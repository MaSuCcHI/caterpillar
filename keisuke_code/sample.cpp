#include <fmt/format.h>
#include <stdio.h>

#include <caterpillar/caterpillar.hpp>
#include <caterpillar/details/utils.hpp>
#include <caterpillar/structures/stg_gate.hpp>
#include <caterpillar/synthesis/decompose_with_ands.hpp>
#include <caterpillar/synthesis/lhrs.hpp>
#include <caterpillar/synthesis/strategies/xag_mapping_strategy.hpp>
#include <caterpillar/synthesis/xag_tracer.hpp>
#include <caterpillar/verification/circuit_to_logic_network.hpp>
#include <fstream>
#include <iostream>
#include <kitty/dynamic_truth_table.hpp>
#include <lorina/verilog.hpp>
#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/algorithms/simulation.hpp>
#include <mockturtle/io/write_dot.hpp>
#include <mockturtle/io/write_verilog.hpp>
#include <mockturtle/mockturtle.hpp>
#include <mockturtle/networks/xag.hpp>
#include <tweedledum/io/qasm.hpp>
#include <tweedledum/io/write_projectq.hpp>
#include <tweedledum/io/write_unicode.hpp>
#include <tweedledum/networks/netlist.hpp>
#include <vector>

void test() { printf("1\n"); }
int main() {
  using namespace caterpillar;
  using namespace mockturtle;
  using namespace tweedledum;

  mockturtle::xag_network xag;
  auto const result = lorina::read_verilog(
      "/Users/kei/Desktop/卒研/pプログラム/Benchmark/arithmetic/adder.v",
      mockturtle::verilog_reader(xag));

  auto strategy = caterpillar::xag_fast_lowt_mapping_strategy();
  tweedledum::netlist<caterpillar::stg_gate> circ;
  caterpillar::logic_network_synthesis(circ, xag, strategy);

  tweedledum::netlist<caterpillar::mcmt_gate> qcirc;
  // caterpillar::decompose_with_ands(qcirc, circ);

  printf("size:%d  qubits:%d gates:%d\n", circ.size(), circ.num_qubits(),
         circ.num_gates());

  // to_qasm
  std::ostringstream s;
  tweedledum::write_qasm(circ, s);

  //ファイル書き込み
  std::ofstream outfile("text.txt");
  outfile << s.str() << std::endl;
  outfile.close();

  printf("HELLO !! \n");
  //   test();
  return 0;
}
