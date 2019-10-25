/*------------------------------------------------------------------------------
| This file is distributed under the MIT License.
| See accompanying file /LICENSE for details.
| Author(s): Giulia Meuli
*-----------------------------------------------------------------------------*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "mapping_strategy.hpp"

#include <mockturtle/utils/progress_bar.hpp>

namespace caterpillar
{

namespace mt = mockturtle;

struct pebbling_mapping_strategy_params
{
  /*! \brief Show progress bar. */
  bool progress{false};

  /*! \brief Maximum number of pebbles to use, if supported by mapping strategy (0 means no limit). */
  uint32_t pebble_limit{0u};

  /*! \brief Conflict limit for the SAT solver (0 means no limit). */
  uint32_t conflict_limit{0u};

  /*! \brief Increment pebble numbers, if a failure occurs. */
  bool increment_on_failure{false};

  /*! \brief Decrement pebble numbers, if satisfiable. */
  bool decrement_on_success{false};

  /*! \brief Maximum number of steps allowed. */
  uint32_t max_steps{1000};
};

/*!
  \verbatim embed:rst
  The pebbling strategy is obtained by solving iteratively the reversible pebbling game on the given network.
  The problem is encoded as a SAT problem. Details can be found in :cite:`MS19`.
  \endverbatim
*/
template<class LogicNetwork, class Solver>
class pebbling_mapping_strategy : public mapping_strategy<LogicNetwork>
{
public:
  pebbling_mapping_strategy( pebbling_mapping_strategy_params const& ps = {} )
    : ps( ps )
  {
    static_assert( mt::is_network_type_v<LogicNetwork>, "LogicNetwork is not a network type" );
    static_assert( mt::has_is_pi_v<LogicNetwork>, "LogicNetwork does not implement the is_pi method" );
    static_assert( mt::has_foreach_fanin_v<LogicNetwork>, "LogicNetwork does not implement the foreach_fanin method" );
    static_assert( mt::has_foreach_gate_v<LogicNetwork>, "LogicNetwork does not implement the foreach_gate method" );
    static_assert( mt::has_num_gates_v<LogicNetwork>, "LogicNetwork does not implement the num_gates method" );
    static_assert( mt::has_foreach_po_v<LogicNetwork>, "LogicNetwork does not implement the foreach_po method" );
    static_assert( mt::has_index_to_node_v<LogicNetwork>, "LogicNetwork does not implement the index_to_node method" );
  }

  bool compute_steps( LogicNetwork const& ntk ) override
  {
    assert( !ps.decrement_on_success || !ps.increment_on_failure );

    std::vector<std::pair<mockturtle::node<LogicNetwork>, mapping_strategy_action>> store_steps;

    auto limit = ps.pebble_limit;


    while ( true )
    {
      Solver solver( ntk, limit );
      solver.init();

      mockturtle::progress_bar bar( 100, "|{0}| current step = {1}", ps.progress );

      typename Solver::result result;

      do
      {
        if ( solver.current_step() >= ps.max_steps )
        {
          result = solver.unknown();
          break;
        }

        bar( std::min<uint32_t>( solver.current_step(), 100 ), solver.current_step() );

        solver.add_step();
        result = solver.solve(); //ps.conflict_limit );
      } while ( result == solver.unsat() );

      if ( result == solver.unknown() )
      {
        if ( ps.increment_on_failure )
        {
          limit++;
          continue;
        }
        else if ( !ps.decrement_on_success )
          return false;
      }
      else if ( result == solver.sat() )
      {
        this->steps() = solver.extract_result();
        if ( ps.decrement_on_success )
        {
          limit--;
          continue;
        }
      }

      if ( this->steps().empty() )
        return false;

      return true;
    }
  }

private:
  pebbling_mapping_strategy_params ps;
};

}
