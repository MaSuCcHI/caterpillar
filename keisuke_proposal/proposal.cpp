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

using namespace std;
using namespace caterpillar;
using namespace mockturtle;
using namespace tweedledum;

/*
 アルゴリズム方針
 1. とりあえずはdecompose_with_andsを参考に効率が悪くてもいいから動くコードを書く！
 2. データ構造などを工夫して効率よくする．
 
 
 - and,xorどちらもtargetを分解する際に必要な要素を配列にまとめておく．
 - controlで使用したものは以降に使用しないようであれば分解する．
 - targetビットは空いてくるビットを優先的に使う
 
  TODO 11/15
    - 初期入力qubitをisCleanQubit=falseに設定する！！
    - clean 同士のcxゲートは計算しないようにする．
 
 
 */

/// 指定したノードが以降の処理で使用されない場合に分解する．
void decompose(netlist<stg_gate>& qcitc,
               uint32_t qubit_id,
               vector<vector<uint32_t>> checkAfteruse,
               vector<bool>& isCleanQubit,
               map<uint32_t,uint32_t>& reallocationIndex,
               map<uint32_t,vector<uint32_t>>& saveToDecompose,
               map<uint32_t,vector<vector<uint32_t>>> resetQubitElement){
    //他の要素の分解で必要な場合分解しない．
    if(saveToDecompose[qubit_id].empty()==false) return;
    
    if(isCleanQubit[qubit_id]==true) return;
    
    //この処理の後のゲートで必要になった場合分解しない．
    int count = 0;
    for(vector<uint32_t> elems : checkAfteruse){
        for(uint32_t elem : elems){
            if(qubit_id == elem)count++;
        }
    }
    // TODO ここコーナーケースありそう．よく考える
    if(count>=2) return;
    
    
    //分解
    qcitc.add_gate(gate::hadamard,qubit_id);
    //機能が足りない（caterpillar には古典ビットが実装されていないため観測結果を使うことができない）
    //qcirc.add_gate(gate::measure,qubit_id,classical_bit);
    isCleanQubit[qubit_id] = true;
    reallocationIndex.erase(qubit_id);
    
    for(vector<uint32_t> elems : resetQubitElement[qubit_id]){
        if(elems.size()==1){
            //観測結果が|1>ならば
            qcitc.add_gate(gate::pauli_z,elems[0]);
            
            auto tmp = saveToDecompose[elems[0]];
            tmp.erase(remove_if(tmp.begin(), tmp.end(), [&](auto& id){return id == qubit_id;}));
        } else if (elems.size()==2) {
            //観測結果が|1>ならば
            qcitc.add_gate(gate::cz,elems[0],elems[1]);
            
            auto tmp = saveToDecompose[elems[0]];
            tmp.erase(remove_if(tmp.begin(), tmp.end(), [&](auto id){return id == qubit_id;}));
            tmp = saveToDecompose[elems[1]];
            tmp.erase(remove_if(tmp.begin(), tmp.end(), [&](auto& id){return id == qubit_id;}));
        }
    }
    
    
    
}

void decompose_with_propose_approach( netlist<stg_gate>& qcirc,
                                     const netlist<stg_gate> circ) {
    
    std::cout << "提案処理"<<std::endl;
    
    //使っているかの確認用
    vector<bool> isCleanQubit(circ.num_qubits(),false);
    
    //他の分解に必要かの確認用
    map<uint32_t,vector<uint32_t>> saveToDecompose;
    
    //使ったことのあるものかを記録
    vector<bool> isUsedFirstTime(circ.num_qubits(),false);
    
    //分解するときにczをかけるべきものを管理
    map<uint32_t,vector<vector<uint32_t>>> resetQubitElement;

    //check repeting ANDs for uncomputation
    vector<tuple<uint32_t,uint32_t,uint32_t>> ash;
    
    //qubitのindex再配置map
    map<uint32_t, uint32_t> reallocationIndex;
    
    
//前処理
    map<uint32_t, uint32_t> q_to_re_id;
    vector<vector<uint32_t>> checkAfterUse;
    
    circ.foreach_cqubit([&] (const uint32_t ip){
        uint32_t q = qcirc.add_qubit();
        q_to_re_id[ip] = q;
    });
    
    
    //入力値の初期状態を not clean + その後の処理で使われるものをチェック
    circ.foreach_cgate([&] (const auto rgate){
        auto cs = rgate.gate.controls();
        auto ts = rgate.gate.targets();
            
        vector<uint32_t> tmp;
        for(uint32_t elem : cs) tmp.push_back(q_to_re_id[elem]);
        for(uint32_t elem : ts) isCleanQubit[q_to_re_id[elem]] = true;
        checkAfterUse.push_back(tmp);
    });
    
    for(uint32_t i=0; i < isCleanQubit.size(); i++){
        if(isCleanQubit[i]==false){
            reallocationIndex[i] = i;
        }
    }
    
    
//本処理
    circ.foreach_cgate([&] (const auto rgate){
        auto cs = rgate.gate.controls();
        auto ts = rgate.gate.targets();
        checkAfterUse.erase(checkAfterUse.begin());
        
        for(uint32_t c : cs){
            if(reallocationIndex.find(c)==end(reallocationIndex))return;
        }
        
        if(reallocationIndex.find(ts[0])==end(reallocationIndex)){
            for(uint32_t i=0;i<isCleanQubit.size();i++){
                if(isCleanQubit[i]){
                    reallocationIndex[ts[0]]=i;
                    break;
                }
            }
            
        }
        
        //CX
        if(rgate.gate.num_controls()==1){
            auto c0 = reallocationIndex[q_to_re_id[cs[0]]];
            auto t0 = reallocationIndex[q_to_re_id[ts[0]]];
            
            if(isCleanQubit[c0]==true)return;
            
            qcirc.add_gate(gate::cx,c0,t0);
            
            auto reset = resetQubitElement[c0];
            for(vector<uint32_t> elems :reset ){
                resetQubitElement[t0].push_back(elems);
                for(uint32_t elem :elems){
                    saveToDecompose[elem].push_back(t0);
                }
            }
            
        }
        //CCX
        else if (rgate.gate.num_controls()==2){
            auto c0 = reallocationIndex[q_to_re_id[cs[0]]];
            auto c1 = reallocationIndex[q_to_re_id[cs[1]]];
            auto t0 = reallocationIndex[q_to_re_id[ts[0]]];
            
            auto tmp_tuple = make_tuple(q_to_re_id[cs[0]],
                       q_to_re_id[cs[1]],
                       q_to_re_id[ts[0]]);
            if(resetQubitElement.find(t0)==end(resetQubitElement)
               && find(ash.begin(),ash.end(),tmp_tuple)==ash.end()){
                //合成
                qcirc.add_gate(gate::mcx,cs,ts);
                vector<uint32_t> resets;
                resets.push_back(c0);
                resets.push_back(c1);
                resetQubitElement[t0].push_back(resets);
                saveToDecompose[c0].push_back(t0);
                saveToDecompose[c1].push_back(t0);
                ash.push_back(tmp_tuple);
                
            } else {
                //分解

                
            }
        }
        auto t0 = reallocationIndex[q_to_re_id[ts[0]]];
        isCleanQubit[t0] = false;
        for(auto control : cs){
            auto c = reallocationIndex[q_to_re_id[control]];
            decompose(qcirc, c, checkAfterUse, isCleanQubit, reallocationIndex, saveToDecompose, resetQubitElement);
        }
    });
    
}

int main(){
    using namespace caterpillar;
    using namespace mockturtle;
    using namespace tweedledum;
    
    mockturtle::xag_network xag;
    auto const result = lorina::read_verilog(
                                             "/Users/kei/Desktop/卒研/pプログラム/Sample/simpleTest.v",
                                             mockturtle::verilog_reader(xag));
    
    
    
    //     量子回路生成
    auto strategy = caterpillar::xag_mapping_strategy();
    tweedledum::netlist<caterpillar::stg_gate> circ;
    caterpillar::logic_network_synthesis(circ, xag, strategy);
    std::ostringstream s0;
    
    tweedledum::write_qasm(circ, s0);
    std::cout<<s0.str()<<std::endl;
    
    
    tweedledum::netlist<caterpillar::stg_gate> rcirc;
    decompose_with_propose_approach(rcirc,circ);
    
    
    // to_qasm
    ostringstream s1;
    tweedledum::write_qasm(rcirc, s1);
    std::cout << s1.str() << std::endl;
    // 回路の規模の確認
    // auto stats = caterpillar::detail::qc_stats(circ, false);
    // printf("size:%d  qubits:%d gates:%d\n", circ.size(), circ.num_qubits(),
    //        circ.num_gates());
    // printf("CNOT:%d Tcount:%d,Tdepth:%d\n", std::get<0>(stats), std::get<1>(stats),
    //        std::get<2>(stats));
    return 0;
}
