/*------------------------------------------------------------------------------
| This file is distributed under the MIT License.
| See accompanying file /LICENSE for details.
| Author(s): Giulia Meuli
*-----------------------------------------------------------------------------*/
#pragma once

#ifdef USE_Z3 

#include <caterpillar/structures/pebbling_view.hpp>
#include <caterpillar/structures/abstract_network.hpp>
#include <caterpillar/synthesis/strategies/action.hpp>
#include <fmt/format.h>

#include <z3++.h>
#include <vector>
#include <type_traits>

#include <mockturtle/networks/klut.hpp>


namespace caterpillar
{
using namespace z3;
using namespace mockturtle;

template<typename Ntk>
class z3_pebble_solver
{
  
  struct variables
  {
    variables( context& ctx )
        : s( expr_vector( ctx ) ), a( expr_vector( ctx ) ) {}

    expr_vector s;
    expr_vector a;
	};

public:

	using node = node<Ntk>;
  using result = z3::check_result;

	z3_pebble_solver(const Ntk& net, const int& pebbles, const int& max_conflicts = 0, const int& max_weight = 0)
	:_net(net), _pebbles(pebbles), _max_weight(max_weight), slv(solver(ctx)), current(variables(ctx)), next(variables(ctx))
	{
		static_assert( has_get_node_v<Ntk>, "Ntk does not implement the get_node method" );
		static_assert( has_foreach_po_v<Ntk>, "Ntk does not implement the foreach_po method" );
		static_assert( has_foreach_node_v<Ntk>, "Ntk does not implement the foreach_node method" );
		static_assert( has_foreach_fanin_v<Ntk>, "Ntk does not implement the foreach_fanin method" );

		ctx.set( "max_conflicts", max_conflicts );

	}

	uint32_t node_to_var( node n )
	{
		return n - ( _net.num_pis() + 1 );
	}

	node var_to_node(uint32_t var)
	{
		return var+_net.num_pis()+1;
	}

	expr_vector new_variable_set(std::string s)
	{
		expr_vector x (ctx);

		_net.foreach_gate([&](auto gate){

			auto x_name = fmt::format("{}_{}_{}", s, num_steps, node_to_var(gate));
			x.push_back( ctx.bool_const( x_name.c_str() ) );
			
		});

		return x;
	}

	void init()
	{
			
		current.s = new_variable_set("s");
		current.a = new_variable_set("a");

		slv.add(!mk_or(current.a));
		slv.add(!mk_or(current.s));
	}

	uint32_t current_step()
	{
		return num_steps;
	}

	result unsat() { return result::unsat; }
	result sat() { return result::sat; }
	result unknown() { return result::unknown; }


	void add_step()
	{
		num_steps+=1;

		next.s = new_variable_set("s");
		next.a = new_variable_set("a");

		for (auto var=0u ; var<next.s.size(); var++)
		{
			_net.foreach_fanin( var_to_node( var ), [&]( auto sig ) {
				auto ch_node = _net.get_node( sig );
				if ( ch_node >= _net.num_pis() + 1 )
				{
					slv.add( implies( current.s[var] != next.s[var], ( current.s[node_to_var( ch_node )] && next.s[node_to_var( ch_node )] ) ) );
				}
			} );

			slv.add( implies( current.s[var] != next.s[var], next.a[var] ) );
			slv.add( implies( current.s[var] == next.s[var], !next.a[var] ) );
		}

		if(_pebbles != 0)	slv.add(atmost(next.s, _pebbles));
		current = next;
	}

	expr_vector weight_expr()
	{
		expr_vector clause (ctx);
		for (uint32_t k=0; k<num_steps+1; k++)
		{
			for (uint32_t i=0; i<current.s.size(); i++)
			{
				for (uint32_t r=0; r<_net.get_weight(var_to_node(i)); r++)
				{
					clause.push_back(ctx.bool_const(fmt::format("a_{}_{}", k, i).c_str()));
				}
			}
		}
		return clause;
	}

	result solve()
	{
		std::vector<uint32_t> o_nodes;

		slv.push();
		_net.foreach_po([&](auto po_sign)
		{
			o_nodes.push_back(_net.get_node(po_sign));
		});

		/* add final clauses */
		for (auto var=0u ; var<next.s.size(); var++)
		{
			if(std::find(o_nodes.begin(), o_nodes.end(), var_to_node(var)) == o_nodes.end())
			{
				slv.add( !current.s[var] );
			}
			else
			{
				slv.add(current.s[var]);
			}
		}

		/* add weight clause */
		if constexpr ( has_get_weight_v<Ntk> )
		{
			if(_max_weight != 0) slv.add(atmost(weight_expr(), _max_weight));
		}

		/* check result (drop final clauses if unsat)*/
		auto result = slv.check();
		if (result == unsat())
		{
			slv.pop();
		}

		return result;
	}


	void print()
	{

		model m = slv.get_model();
		uint32_t w = 0;

		for(uint32_t n=0; n<current.s.size(); n++)
		{
			std::cout << std::endl;
			for(uint32_t k =0; k<num_steps+1; k++)
			{
				auto s = fmt::format("s_{}_{}", k, n);
				auto s_var = m.eval(ctx.bool_const(s.c_str()));
				if (s_var.is_true()) std::cout << "1" << "-";
				else std::cout << "0" << "-";

				auto a = fmt::format("a_{}_{}", k, n);
				auto a_var = m.eval(ctx.bool_const(a.c_str()));
				if (_max_weight !=0)
				{
					if (a_var.is_true()) 
					{
						w += _net.get_weight(var_to_node(n));
						std::cout << "y" << "+" << _net.get_weight(var_to_node(n)) << " " ;
					}
					else std::cout << "n" << "+0 ";
				}
				
			}
		}
		std::cout << fmt::format("\nTOT.Weight = {}\n", w);
	}

	std::vector<std::pair<mockturtle::node<pebbling_view<Ntk>>, mapping_strategy_action>> extract_result( bool verbose = false)
	{
		model m = slv.get_model();
		std::vector<std::pair<mockturtle::node<pebbling_view<Ntk>>, mapping_strategy_action>> steps;

		for (uint32_t k = 0; k <num_steps+1; k++)
		{
			std::vector<std::pair<uint32_t, bool>> step_action;

			for (uint32_t i = 0; i< current.s.size(); i++)
			{
				auto a_var = fmt::format("a_{}_{}", k, i).c_str();
				if( m.eval(ctx.bool_const(a_var)).is_true())
				{
					bool s_pre = m.eval( ctx.bool_const( fmt::format("s_{}_{}", k-1, i).c_str() )).is_true();
					bool s_cur = m.eval( ctx.bool_const( fmt::format("s_{}_{}", k, i).c_str() )).is_true();
					assert (s_pre != s_cur);

					step_action.push_back(std::make_pair(i, s_cur));
				}
			}

			/*sort step actions to have all the deactivations (false) first */
			std::sort(step_action.begin(), step_action.end(), 
				[](const std::pair<uint32_t, bool>& first, const std::pair<uint32_t, bool>& second)
				{
					(void)second;
					if(!first.second)	return true;
					else return false;
				}
			);

			/* add actions to the pebbling strategy */
			for(auto act : step_action)
			{
				auto act_node = var_to_node(act.first);

				if(act.second)
				{
					steps.push_back({act_node, compute_action{}});
					if( verbose ) std::cout << "compute on node " <<  act_node << std::endl;
				}
				else
				{ 
					steps.push_back({act_node, uncompute_action{}});
					if( verbose ) std::cout << "uncompute on node " <<  act_node << std::endl;

				}
			}
		}


		return steps;
	}


private:
const Ntk _net;
const int _pebbles;
const int _max_weight;

context ctx;
solver slv;

uint32_t num_steps = 0;
variables current;
variables next;

};

/* template specifications to handle the mockturtle::klut_network structure */
template<>
inline uint32_t z3_pebble_solver<klut_network>::node_to_var(mockturtle::node<klut_network> n)
{
	return n-(this ->_net.num_pis()+2);
}

template<>
inline mockturtle::node<klut_network> z3_pebble_solver<klut_network>::var_to_node(uint32_t var)
{
	return var+this ->_net.num_pis()+2;
}

template<>
inline void z3_pebble_solver<klut_network>::add_step()
{
	num_steps+=1;

	next.s = new_variable_set("s");
	next.a = new_variable_set("a");

	for (auto var=0u ; var<next.s.size(); var++)
	{
		_net.foreach_fanin( var_to_node( var ), [&]( auto sig ) {
			auto ch_node = _net.get_node( sig );
			if ( ch_node >= _net.num_pis() + 2 )
			{
				slv.add( implies( current.s[var] != next.s[var], ( current.s[node_to_var( ch_node )] && next.s[node_to_var( ch_node )] ) ) );
			}
		} );

		slv.add( implies( current.s[var] != next.s[var], next.a[var] ) );
		slv.add( implies( current.s[var] == next.s[var], !next.a[var] ) );
	}

	if (_pebbles != 0)	slv.add( atmost( next.s, _pebbles ) );
	current = next;
}

}
#endif