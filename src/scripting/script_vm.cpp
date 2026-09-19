#include "opengenesis/scripting/script_vm.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace opengenesis::scripting {
namespace {

std::string trim(std::string value) {
    const auto nonspace=[](char c){return std::isspace(static_cast<unsigned char>(c))==0;};
    value.erase(value.begin(),std::find_if(value.begin(),value.end(),nonspace));
    value.erase(std::find_if(value.rbegin(),value.rend(),nonspace).base(),value.end());
    return value;
}

bool atom(std::string_view value,std::size_t max=128) {
    if(value.empty()||value.size()>max) return false;
    return std::all_of(value.begin(),value.end(),[](char c){
        const auto u=static_cast<unsigned char>(c);
        return std::isalnum(u)!=0||c=='_'||c=='-'||c=='.'||c==':';
    });
}

std::optional<std::int64_t> integer(std::string_view text) {
    std::int64_t value=0;
    const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(ec!=std::errc{}||end!=text.data()+text.size()) return std::nullopt;
    return value;
}

std::size_t state_bytes(const ScriptVmState& state) {
    std::size_t total=state.state.size();
    for(const auto& [key,value]:state.variables) total+=key.size()+value.size();
    return total;
}

std::string resolve(std::string_view value,const ScriptVmState& state) {
    if(value.size()>1 && value.front()=='$') {
        const auto it=state.variables.find(std::string{value.substr(1)});
        return it==state.variables.end()?std::string{}:it->second;
    }
    return std::string{value};
}

std::string hex(std::string_view input) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out;
    out.reserve(input.size()*2);
    for(unsigned char c:input){out.push_back(digits[c>>4]);out.push_back(digits[c&0x0f]);}
    return out;
}

std::optional<std::string> unhex(std::string_view input) {
    if((input.size()%2)!=0) return std::nullopt;
    auto nibble=[](char c)->int{
        if(c>='0'&&c<='9') return c-'0';
        if(c>='a'&&c<='f') return c-'a'+10;
        if(c>='A'&&c<='F') return c-'A'+10;
        return -1;
    };
    std::string out(input.size()/2,'\0');
    for(std::size_t i=0;i<out.size();++i){
        const int hi=nibble(input[i*2]),lo=nibble(input[i*2+1]);
        if(hi<0||lo<0) return std::nullopt;
        out[i]=static_cast<char>((hi<<4)|lo);
    }
    return out;
}

} // namespace

std::optional<CompiledScript> compile_script(std::string_view source,std::string& reason) {
    if(source.empty()||source.size()>64U*1024U){reason="invalid-script-size";return std::nullopt;}
    CompiledScript result;
    ScriptHandler* current=nullptr;
    std::istringstream input(std::string{source});
    std::string line;
    std::size_t lines=0;
    while(std::getline(input,line)){
        if(++lines>2048){reason="script-too-many-lines";return std::nullopt;}
        line=trim(std::move(line));
        if(line.empty()||line.starts_with("#")) continue;
        std::istringstream parts(line);
        std::string op;
        parts>>op;
        if(op=="event"){
            std::string event; parts>>event;
            if(!atom(event)){reason="invalid-event";return std::nullopt;}
            result.handlers.push_back({.event=std::move(event),.instructions={}});
            current=&result.handlers.back();
            continue;
        }
        if(op=="end"){current=nullptr;continue;}
        if(!current){reason="instruction-outside-event";return std::nullopt;}
        ScriptInstruction ins;
        if(op=="set"){
            ins.opcode=ScriptOpcode::set; parts>>ins.a; std::getline(parts,ins.b); ins.b=trim(ins.b);
            if(!atom(ins.a)||ins.b.size()>2048){reason="invalid-set";return std::nullopt;}
        } else if(op=="add"){
            ins.opcode=ScriptOpcode::add; parts>>ins.a>>ins.b;
            if(!atom(ins.a)||!integer(ins.b)){reason="invalid-add";return std::nullopt;}
        } else if(op=="emit"){
            ins.opcode=ScriptOpcode::emit; std::getline(parts,ins.a); ins.a=trim(ins.a);
            if(ins.a.size()>2048){reason="invalid-emit";return std::nullopt;}
        } else if(op=="state"){
            ins.opcode=ScriptOpcode::state; parts>>ins.a;
            if(!atom(ins.a)){reason="invalid-state";return std::nullopt;}
        } else if(op=="timer"){
            ins.opcode=ScriptOpcode::timer; parts>>ins.a;
            const auto value=integer(ins.a);
            if(!value||*value<0||*value>86400000){reason="invalid-timer";return std::nullopt;}
        } else if(op=="listen"){
            ins.opcode=ScriptOpcode::listen; parts>>ins.a;
            const auto value=integer(ins.a);
            if(!value||*value<std::numeric_limits<std::int32_t>::min()||*value>std::numeric_limits<std::int32_t>::max()){
                reason="invalid-listen";return std::nullopt;
            }
        } else if(op=="notify"){
            ins.opcode=ScriptOpcode::notify; std::getline(parts,ins.a); ins.a=trim(ins.a);
            if(ins.a.empty()||ins.a.size()>1000){reason="invalid-notify";return std::nullopt;}
        } else if(op=="message"){
            ins.opcode=ScriptOpcode::message; parts>>ins.a; std::getline(parts,ins.b); ins.b=trim(ins.b);
            if(!atom(ins.a,256)||ins.b.empty()||ins.b.size()>2000){reason="invalid-message";return std::nullopt;}
        } else if(op=="move"||op=="rotate"||op=="scale"||
                  op=="velocity"||op=="angular_velocity"){
            ins.opcode=op=="move"?ScriptOpcode::move:
                       (op=="rotate"?ScriptOpcode::rotate:
                        (op=="scale"?ScriptOpcode::scale:
                         (op=="velocity"?ScriptOpcode::velocity:
                                          ScriptOpcode::angular_velocity)));
            parts>>ins.a>>ins.b>>ins.c;
            std::string extra; parts>>extra;
            if(ins.a.empty()||ins.b.empty()||ins.c.empty()||!extra.empty()||
               ins.a.size()>128||ins.b.size()>128||ins.c.size()>128){
                reason="invalid-world-vector";return std::nullopt;
            }
        } else if(op=="physics"){
            ins.opcode=ScriptOpcode::physics; parts>>ins.a;
            if(ins.a!="0"&&ins.a!="1"){reason="invalid-physics";return std::nullopt;}
        } else if(op=="text"){
            ins.opcode=ScriptOpcode::text; std::getline(parts,ins.a); ins.a=trim(ins.a);
            if(ins.a.size()>512){reason="invalid-object-text";return std::nullopt;}
        } else if(op=="say"||op=="whisper"||op=="shout"){
            ins.opcode=op=="say"?ScriptOpcode::say:(op=="whisper"?ScriptOpcode::whisper:ScriptOpcode::shout);
            std::getline(parts,ins.a); ins.a=trim(ins.a);
            if(ins.a.empty()||ins.a.size()>512){reason="invalid-chat";return std::nullopt;}
        } else if(op=="object_info"||op=="region_info"||
                  op=="terrain_height"||op=="water_level"||
                  op=="world_time"){
            ins.opcode=op=="object_info"?ScriptOpcode::object_info:
                       (op=="region_info"?ScriptOpcode::region_info:
                        (op=="terrain_height"?ScriptOpcode::terrain_height:
                         (op=="water_level"?ScriptOpcode::water_level:
                                             ScriptOpcode::world_time)));
            parts>>ins.a;
            std::string extra; parts>>extra;
            if(!atom(ins.a,64)||!extra.empty()){reason="invalid-world-query";return std::nullopt;}
        } else if(op=="nearby_avatars"){
            ins.opcode=ScriptOpcode::nearby_avatars;
            parts>>ins.a>>ins.b;
            std::string extra; parts>>extra;
            const auto radius=integer(ins.b);
            if(!atom(ins.a,64)||!radius||*radius<1||*radius>96||!extra.empty()){
                reason="invalid-nearby-query";return std::nullopt;
            }
        } else if(op=="stop"){
            ins.opcode=ScriptOpcode::stop;
        } else {
            reason="unknown-opcode";return std::nullopt;
        }
        current->instructions.push_back(std::move(ins));
        if(current->instructions.size()>1024){reason="handler-too-large";return std::nullopt;}
    }
    if(result.handlers.empty()){reason="no-event-handlers";return std::nullopt;}
    reason.clear();
    return result;
}

ScriptVmResult execute_script_event(const CompiledScript& program,std::string_view event,
                                    const ScriptVmState& initial_state,const ScriptVmLimits& limits) {
    ScriptVmResult result{.ok=false,.error={},.instructions_executed=0,.state=initial_state,.actions={}};
    const auto handler=std::find_if(program.handlers.begin(),program.handlers.end(),
        [&](const ScriptHandler& candidate){return candidate.event==event;});
    if(handler==program.handlers.end()){result.ok=true;return result;}

    for(const auto& ins:handler->instructions){
        if(result.instructions_executed>=limits.instruction_budget){result.error="instruction-budget-exceeded";return result;}
        ++result.instructions_executed;
        if(ins.opcode==ScriptOpcode::stop) break;
        if(ins.opcode==ScriptOpcode::set){
            if(!result.state.variables.contains(ins.a) && result.state.variables.size()>=limits.max_variables){
                result.error="variable-budget-exceeded";return result;
            }
            result.state.variables[ins.a]=resolve(ins.b,result.state);
        } else if(ins.opcode==ScriptOpcode::add){
            const auto delta=integer(ins.b);
            const auto current=integer(result.state.variables.contains(ins.a)?result.state.variables.at(ins.a):"0");
            if(!delta||!current){result.error="numeric-state-required";return result;}
            result.state.variables[ins.a]=std::to_string(*current+*delta);
        } else if(ins.opcode==ScriptOpcode::emit){
            if(result.actions.size()>=limits.max_output_actions){result.error="action-budget-exceeded";return result;}
            result.actions.push_back({.type=ScriptActionType::emit,.value=resolve(ins.a,result.state),.number=0});
        } else if(ins.opcode==ScriptOpcode::state){
            result.state.state=ins.a;
            result.actions.push_back({.type=ScriptActionType::state_change,.value=ins.a,.number=0});
        } else if(ins.opcode==ScriptOpcode::timer){
            result.actions.push_back({.type=ScriptActionType::set_timer,.value={},.number=integer(ins.a).value_or(0)});
        } else if(ins.opcode==ScriptOpcode::listen){
            result.actions.push_back({.type=ScriptActionType::listen,.value={},.number=integer(ins.a).value_or(0)});
        } else if(ins.opcode==ScriptOpcode::notify){
            result.actions.push_back({.type=ScriptActionType::notify_owner,.value=resolve(ins.a,result.state),.number=0});
        } else if(ins.opcode==ScriptOpcode::message){
            result.actions.push_back({.type=ScriptActionType::direct_message,
                                      .value=ins.a+"\n"+resolve(ins.b,result.state),.number=0});
        } else if(ins.opcode==ScriptOpcode::move||
                  ins.opcode==ScriptOpcode::rotate||
                  ins.opcode==ScriptOpcode::scale||
                  ins.opcode==ScriptOpcode::velocity||
                  ins.opcode==ScriptOpcode::angular_velocity){
            const auto type=ins.opcode==ScriptOpcode::move?ScriptActionType::world_move:
                            (ins.opcode==ScriptOpcode::rotate?ScriptActionType::world_rotate:
                             (ins.opcode==ScriptOpcode::scale?ScriptActionType::world_scale:
                              (ins.opcode==ScriptOpcode::velocity?ScriptActionType::world_velocity:
                                                                        ScriptActionType::world_angular_velocity)));
            result.actions.push_back({.type=type,
                                      .value=resolve(ins.a,result.state)+" "+
                                             resolve(ins.b,result.state)+" "+
                                             resolve(ins.c,result.state),
                                      .number=0});
        } else if(ins.opcode==ScriptOpcode::physics){
            result.actions.push_back({.type=ScriptActionType::world_physics,
                                      .value=ins.a,.number=0});
        } else if(ins.opcode==ScriptOpcode::text){
            result.actions.push_back({.type=ScriptActionType::world_text,
                                      .value=resolve(ins.a,result.state),.number=0});
        } else if(ins.opcode==ScriptOpcode::say||
                  ins.opcode==ScriptOpcode::whisper||
                  ins.opcode==ScriptOpcode::shout){
            const auto type=ins.opcode==ScriptOpcode::say?ScriptActionType::world_chat_say:
                            (ins.opcode==ScriptOpcode::whisper?ScriptActionType::world_chat_whisper:
                                                               ScriptActionType::world_chat_shout);
            result.actions.push_back({.type=type,.value=resolve(ins.a,result.state),.number=0});
        } else if(ins.opcode==ScriptOpcode::object_info||
                  ins.opcode==ScriptOpcode::region_info||
                  ins.opcode==ScriptOpcode::terrain_height||
                  ins.opcode==ScriptOpcode::water_level||
                  ins.opcode==ScriptOpcode::world_time){
            const auto type=ins.opcode==ScriptOpcode::object_info?ScriptActionType::world_query_object:
                            (ins.opcode==ScriptOpcode::region_info?ScriptActionType::world_query_region:
                             (ins.opcode==ScriptOpcode::terrain_height?ScriptActionType::world_query_terrain:
                              (ins.opcode==ScriptOpcode::water_level?ScriptActionType::world_query_water:
                                                                    ScriptActionType::world_query_time)));
            result.actions.push_back({.type=type,.value=ins.a,.number=0});
        } else if(ins.opcode==ScriptOpcode::nearby_avatars){
            result.actions.push_back({.type=ScriptActionType::world_query_nearby,
                                      .value=ins.a+"|"+ins.b,.number=0});
        }
        if(state_bytes(result.state)>limits.max_state_bytes){result.error="state-budget-exceeded";return result;}
        if(result.actions.size()>limits.max_output_actions){result.error="action-budget-exceeded";return result;}
    }
    result.ok=true;
    return result;
}

std::string serialize_vm_state(const ScriptVmState& state) {
    std::vector<std::pair<std::string,std::string>> values(state.variables.begin(),state.variables.end());
    std::sort(values.begin(),values.end());
    std::ostringstream out;
    out<<hex(state.state);
    for(const auto& [key,value]:values) out<<';'<<hex(key)<<'='<<hex(value);
    return out.str();
}

std::optional<ScriptVmState> deserialize_vm_state(std::string_view encoded,std::string& reason) {
    ScriptVmState state;
    const auto first=encoded.find(';');
    const auto state_hex=encoded.substr(0,first);
    const auto decoded_state=unhex(state_hex);
    if(!decoded_state||!atom(*decoded_state)){reason="invalid-vm-state";return std::nullopt;}
    state.state=*decoded_state;
    std::size_t pos=first==std::string_view::npos?encoded.size():first+1;
    while(pos<encoded.size()){
        const auto end=encoded.find(';',pos);
        const auto part=encoded.substr(pos,end==std::string_view::npos?encoded.size()-pos:end-pos);
        const auto eq=part.find('=');
        if(eq==std::string_view::npos){reason="invalid-vm-variable";return std::nullopt;}
        const auto key=unhex(part.substr(0,eq));
        const auto value=unhex(part.substr(eq+1));
        if(!key||!value||!atom(*key)){reason="invalid-vm-variable";return std::nullopt;}
        state.variables[*key]=*value;
        if(end==std::string_view::npos) break;
        pos=end+1;
    }
    reason.clear();
    return state;
}

} // namespace opengenesis::scripting
