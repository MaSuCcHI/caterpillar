#include <fmt/format.h>
#include <stdio.h>

#include <caterpillar/caterpillar.hpp>
#include <caterpillar/details/utils.hpp>
#include <caterpillar/structures/stg_gate.hpp>
#include <caterpillar/synthesis/decompose_with_ands.hpp>
#include <caterpillar/synthesis/lhrs.hpp>
#include <caterpillar/synthesis/strategies/bennett_mapping_strategy.hpp>
#include <caterpillar/synthesis/strategies/xag_mapping_strategy.hpp>
#include <caterpillar/synthesis/xag_tracer.hpp>
#include <caterpillar/verification/circuit_to_logic_network.hpp>
#include <fstream>
#include <iostream>
#include <tuple>
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
#include <map>

void decompose_with_propose_approach( tweedledum::netlist<caterpillar::stg_gate>& rcirc,
                                     tweedledum::netlist<caterpillar::stg_gate> circ) {
    using namespace std;
    using namespace caterpillar;
    using namespace mockturtle;
    using namespace tweedledum;
    std::cout << "提案処理"<<std::endl;
    
    //使っているかの確認用
    vector<bool> used(circ.num_qubits(),false);
    
    //他の分解に必要かの確認用
    vector<bool> saveToDecompose(circ.num_qubits(),false);
    
    //使ったことのあるものかを記録
    vector<bool> isUsedFirstTime(circ.num_qubits(),false);
    
    //xorを分解するときにczをかけるべきものを管理
    map<int,vector<vector<int>>> decomposeXorGateElement;
    
    
    
    //処理
    circ.foreach_cinput([&] (const auto ip){
        rcirc.add_qubit();
        
    });
    
}

int main(){
    using namespace caterpillar;
    using namespace mockturtle;
    using namespace tweedledum;
    
    mockturtle::xag_network xag;
    auto const result = lorina::read_verilog(
                                             "/Users/kei/Desktop/卒研/pプログラム/Benchmark/arithmetic/adder.v",
                                             mockturtle::verilog_reader(xag));
    
    
    
    //     量子回路生成
    auto strategy = caterpillar::xag_mapping_strategy();
    tweedledum::netlist<caterpillar::stg_gate> circ;
    caterpillar::logic_network_synthesis(circ, xag, strategy);
    
    tweedledum::netlist<caterpillar::stg_gate> rcirc;
    decompose_with_propose_approach(rcirc,circ);
    
    
    // 回路の規模の確認
    // auto stats = caterpillar::detail::qc_stats(circ, false);
    // printf("size:%d  qubits:%d gates:%d\n", circ.size(), circ.num_qubits(),
    //        circ.num_gates());
    // printf("CNOT:%d Tcount:%d,Tdepth:%d\n", std::get<0>(stats), std::get<1>(stats),
    //        std::get<2>(stats));
    return 0;
}
