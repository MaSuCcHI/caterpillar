/* mockturtle: C++ logic network library
 * Copyright (C) 2018-2019  EPFL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*!
  \file depth_view.hpp
  \brief Implements depth and level for a network

  \author Mathias Soeken
*/

#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>
#include <fmt/format.h>

#include "../traits.hpp"
#include "../utils/node_map.hpp"
#include "immutable_view.hpp"
#include <mockturtle/networks/xag.hpp>

namespace mockturtle
{

struct depth_view_params
{
  bool count_complements{false};

  bool compute_m_critical_path{false};
};

/*! \brief Implements `depth` and `level` methods for networks.
 *
 * This view computes the level of each node and also the depth of
 * the network.  It implements the network interface methods
 * `level` and `depth`.  The levels are computed at construction
 * and can be recomputed by calling the `update_levels` method.
 *
 * **Required network functions:**
 * - `size`
 * - `get_node`
 * - `visited`
 * - `set_visited`
 * - `foreach_fanin`
 * - `foreach_po`
 *
 * Example
 *
   \verbatim embed:rst

   .. code-block:: c++

      // create network somehow
      aig_network aig = ...;

      // create a depth view on the network
      depth_view aig_depth{aig};

      // print depth
      std::cout << "Depth: " << aig_depth.depth() << "\n";
   \endverbatim
 */
template<typename Ntk, bool has_depth_interface = has_depth_v<Ntk>&& has_level_v<Ntk>&& has_update_levels_v<Ntk>>
class depth_view
{
};

template<typename Ntk>
class depth_view<Ntk, true> : public Ntk
{
public:
  depth_view( Ntk const& ntk, depth_view_params const& ps = {} ) : Ntk( ntk )
  {
    (void)ps;
  }
};

template<typename Ntk>
class depth_view<Ntk, false> : public Ntk
{
public:
  using storage = typename Ntk::storage;
  using node = typename Ntk::node;
  using signal = typename Ntk::signal;

  /*! \brief Standard constructor.
   *
   * \param ntk Base network
   * \param count_complements Count inverters as 1
   */
  explicit depth_view( Ntk const& ntk, depth_view_params const& ps = {} )
      : Ntk( ntk ),
        _ps( ps ),
        _levels( ntk ),
        _crit_path( ntk ),
        _m_levels( ntk ),
        _m_crit_path(ntk)
  {
    static_assert( is_network_type_v<Ntk>, "Ntk is not a network type" );
    static_assert( has_size_v<Ntk>, "Ntk does not implement the size method" );
    static_assert( has_get_node_v<Ntk>, "Ntk does not implement the get_node method" );
    static_assert( has_is_complemented_v<Ntk>, "Ntk does not implement the is_complemented method" );
    static_assert( has_visited_v<Ntk>, "Ntk does not implement the visited method" );
    static_assert( has_set_visited_v<Ntk>, "Ntk does not implement the set_visited method" );
    static_assert( has_foreach_po_v<Ntk>, "Ntk does not implement the foreach_po method" );
    static_assert( has_foreach_fanin_v<Ntk>, "Ntk does not implement the foreach_fanin method" );

    update_levels();
    if constexpr ( std::is_base_of_v<xag_network, Ntk>)
    {
      this -> clear_visited();
      update_m_levels();
    }
  }

  uint32_t depth() const
  {
    return _depth;
  }

  uint32_t level( node const& n ) const
  {
    return _levels[n];
  }

  bool is_on_critical_path( node const& n ) const
  {
    return _crit_path[n];
  }

  void set_level( node const& n, uint32_t level )
  {
    _levels[n] = level;
  }

  void update_levels()
  {
    _levels.reset( 0 );
    _crit_path.reset( false );

    this->incr_trav_id();
    compute_levels();
  }

  void resize_levels()
  {
    _levels.resize();
  }

#pragma region Multiplicative depth

  uint32_t m_depth() const
  {
    return _m_depth;
  }

  uint32_t m_level( node const& n ) const
  {
    return _m_levels[n];
  }


  void update_m_levels()
  {
    _m_levels.reset( 0 );

    compute_m_levels();
  }

  bool is_on_critical_m_path( node const& n ) const
  {
    return _m_crit_path[n];
  }

  void set_is_on_critical_m_path( node const& n) const
  {
    _m_crit_path[n] = true; 
  }

  void reset_is_on_critical_m_path( node const& n) const
  {
    _m_crit_path[n] = false; 
  }

#pragma endregion

private:
  uint32_t compute_levels( node const& n )
  {
    if ( this->visited( n ) == this->trav_id() )
    {
      return _levels[n];
    }
    this->set_visited( n, this->trav_id() );

    if ( this->is_constant( n ) || this->is_pi( n ) )
    {
      return _levels[n] = 0;
    }

    uint32_t level{0};
    this->foreach_fanin( n, [&]( auto const& f ) {
      auto clevel = compute_levels( this->get_node( f ) );
      if ( _ps.count_complements && this->is_complemented( f ) )
      {
        clevel++;
      }
      level = std::max( level, clevel );
    } );

    return _levels[n] = level + 1;
  }

  void compute_levels()
  {
    _depth = 0;
    this->foreach_po( [&]( auto const& f ) {
      auto clevel = compute_levels( this->get_node( f ) );
      if ( _ps.count_complements && this->is_complemented( f ) )
      {
        clevel++;
      }
      _depth = std::max( _depth, clevel );
    } );

    this->foreach_po( [&]( auto const& f ) {
      const auto n = this->get_node( f );
      if ( _levels[n] == _depth )
      {
        set_critical_path( n );
      }
    } );
  }

  void set_critical_path( node const& n )
  {
    _crit_path[n] = true;
    if ( !this->is_constant( n ) && !this->is_pi( n ) )
    {
      const auto lvl = _levels[n];
      this->foreach_fanin( n, [&]( auto const& f ) {
        const auto cn = this->get_node( f );
        const auto offset = _ps.count_complements && this->is_complemented( f ) ? 2u : 1u;
        if ( _levels[cn] + offset == lvl && !_crit_path[cn] )
        {
          set_critical_path( cn );
        }
      } );
    }
  }

  depth_view_params _ps;
  node_map<uint32_t, Ntk> _levels;
  node_map<uint32_t, Ntk> _crit_path;
  uint32_t _depth;


#pragma region Compute Multiplicative Levels

  uint32_t compute_m_levels( node const& n )
  {
    if ( this->visited( n ) == this->trav_id() )
    {
      return _m_levels[n];
    }
    this->set_visited( n, this->trav_id() );

    if ( this->is_constant( n ) || this->is_pi( n ) )
    {
      return _m_levels[n] = 0;
    }

    /* get maximum level of fanins */
    uint32_t level{0};
    this->foreach_fanin( n, [&]( auto const& f ) {
      auto clevel = compute_m_levels( this->get_node( f ) );
      level = std::max( level, clevel );
    } );

    

    if constexpr ( std::is_base_of_v<xag_network, Ntk >)
      return (this -> is_and(n)) ? _m_levels[n] = level + 1 : _m_levels[n] = level;
    else
      return _m_levels[n] = level + 1;
    
  }
  void compute_m_critical_path ( uint32_t const& node, uint32_t ref_level)
  {
    if( this -> is_pi(node) || this -> is_constant(node) ) 
      return;
    
    if( this -> is_and(node) && _m_levels[node] == ref_level)
    {
      _m_crit_path[node] = true; 
      ref_level--;
    }

    this -> foreach_fanin(node, [&] (auto const& s)
    {
      compute_m_critical_path( this -> get_node(s), ref_level);
    });
  }

  void compute_m_levels()
  {
    _m_depth = 0;
    this->foreach_po( [&]( auto const& f ) {
      auto clevel = compute_m_levels( this->get_node( f ) );
      _m_depth = std::max( _m_depth, clevel );
    } );

    /* compute critical path */
    if(_ps.compute_m_critical_path)
    {
      this -> foreach_po( [&] (auto const& f ) {
        compute_m_critical_path( this -> get_node(f), _m_depth );
      });
    }
  }

  node_map<uint32_t, Ntk> _m_levels;
  node_map<uint32_t, Ntk> _m_crit_path;
  uint32_t _m_depth;

#pragma endregion

};

template<class T>
depth_view( T const& )->depth_view<T>;

template<class T>
depth_view( T const&, depth_view_params const& )->depth_view<T>;

} // namespace mockturtle
