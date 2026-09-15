#include "opengenesis/world/scene_server.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/security/scene_ticket.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace opengenesis::world {
namespace protocol = opengenesis::protocol;

class SceneAuthContext final {
public:
    explicit SceneAuthContext(std::string secret) : secret_(std::move(secret)) {
        if (secret_.size() < 32) throw std::runtime_error("scene ticket secret must be at least 32 bytes");
    }
    std::optional<security::SceneTicketClaims> consume(std::string_view token, std::string_view region) {
        const auto claims = security::verify_scene_ticket(secret_, token, region);
        if (!claims) return std::nullopt;
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::scoped_lock lock(mutex_);
        for (auto it=used_.begin();it!=used_.end();) { if(it->second<=now) it=used_.erase(it); else ++it; }
        if (used_.contains(claims->nonce)) return std::nullopt;
        used_[claims->nonce] = claims->expires_unix;
        return claims;
    }
private:
    std::string secret_;
    std::mutex mutex_;
    std::unordered_map<std::string,std::int64_t> used_;
};

namespace {
std::string field(const std::string& payload, const std::string& key) {
    std::istringstream input(payload); std::string line;
    while (std::getline(input,line)) { const auto split=line.find('='); if(split!=std::string::npos&&line.substr(0,split)==key)return line.substr(split+1); }
    return {};
}
double number(const std::string& payload,const std::string& key,double fallback){const auto v=field(payload,key);if(v.empty())return fallback;try{double p=std::stod(v);return std::isfinite(p)?p:fallback;}catch(...){return fallback;}}
std::uint64_t integer(const std::string& payload,const std::string& key,std::uint64_t fallback=0){const auto v=field(payload,key);if(v.empty())return fallback;try{return std::stoull(v);}catch(...){return fallback;}}
std::string clean(std::string value,std::size_t max_length=96){value.erase(std::remove_if(value.begin(),value.end(),[](char c){return c=='\n'||c=='\r'||c=='|';}),value.end());if(value.size()>max_length)value.resize(max_length);return value;}
Transform transform_from_payload(const std::string& payload,const Transform& fallback={}){Transform t=fallback;t.position.x=number(payload,"x",fallback.position.x);t.position.y=number(payload,"y",fallback.position.y);t.position.z=number(payload,"z",fallback.position.z);t.rotation.x=number(payload,"rx",fallback.rotation.x);t.rotation.y=number(payload,"ry",fallback.rotation.y);t.rotation.z=number(payload,"rz",fallback.rotation.z);t.scale.x=std::max(0.01,number(payload,"sx",fallback.scale.x));t.scale.y=std::max(0.01,number(payload,"sy",fallback.scale.y));t.scale.z=std::max(0.01,number(payload,"sz",fallback.scale.z));return t;}
std::string serialize_snapshot(const RegionRuntime& region){const auto entities=region.snapshot_entities();std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<"region="<<region.id()<<'\n'<<"sequence="<<region.latest_sequence()<<'\n'<<"terrain_width="<<region.terrain().width()<<'\n'<<"terrain_height="<<region.terrain().height()<<'\n'<<"terrain_cell_size="<<region.terrain().cell_size()<<'\n'<<"terrain_revision="<<region.terrain().revision()<<'\n'<<"entity_count="<<entities.size()<<'\n';for(const auto&e:entities){const auto&t=e.transform;o<<"entity="<<e.id<<'|'<<entity_kind_name(e.kind)<<'|'<<clean(e.name)<<'|'<<t.position.x<<'|'<<t.position.y<<'|'<<t.position.z<<'|'<<t.rotation.x<<'|'<<t.rotation.y<<'|'<<t.rotation.z<<'|'<<t.scale.x<<'|'<<t.scale.y<<'|'<<t.scale.z<<'|'<<clean(e.owner_user_id,64)<<'\n';}return o.str();}
std::string serialize_events(const RegionRuntime& region,std::uint64_t since){const auto events=region.events_since(since);std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<"region="<<region.id()<<'\n'<<"from="<<since<<'\n'<<"latest="<<region.latest_sequence()<<'\n'<<"count="<<events.size()<<'\n';for(const auto&e:events){const auto&t=e.transform;o<<"event="<<e.sequence<<'|'<<e.type<<'|'<<e.entity_id<<'|'<<clean(e.text,512)<<'|'<<t.position.x<<'|'<<t.position.y<<'|'<<t.position.z<<'|'<<t.rotation.x<<'|'<<t.rotation.y<<'|'<<t.rotation.z<<'|'<<t.scale.x<<'|'<<t.scale.y<<'|'<<t.scale.z<<'\n';}return o.str();}

void handle_client(opengenesis::network::TcpSocket socket,std::unordered_map<std::string,std::shared_ptr<RegionRuntime>> regions,std::shared_ptr<SceneAuthContext> auth){
    std::shared_ptr<RegionRuntime> region; std::uint64_t avatar_id=0; std::string user_id;
    try{
        const auto hello=socket.receive_frame();if(hello.type!=protocol::MessageType::hello)throw std::runtime_error("scene HELLO required");
        socket.send_frame({protocol::MessageType::hello_ack,hello.request_id,protocol::payload_from_string("protocol=1\nserver=opengenesis-scene\nauth=scene-ticket-v1\n")});
        const auto join=socket.receive_frame();if(join.type!=protocol::MessageType::scene_join)throw std::runtime_error("SCENE_JOIN required");
        const auto body=protocol::payload_as_string(join);const auto region_id=field(body,"region");const auto it=regions.find(region_id);
        if(it==regions.end()){socket.send_frame({protocol::MessageType::error,join.request_id,protocol::payload_from_string("reason=unknown-region\n")});return;}
        const auto claims=auth->consume(field(body,"ticket"),region_id);
        if(!claims){socket.send_frame({protocol::MessageType::error,join.request_id,protocol::payload_from_string("reason=invalid-scene-ticket\n")});return;}
        region=it->second;user_id=claims->user_id;Transform spawn{};spawn.position={number(body,"x",128.0),number(body,"y",128.0),number(body,"z",0.0)};
        avatar_id=region->spawn_avatar(user_id,clean(claims->display_name),spawn);
        std::ostringstream joined;joined<<"status=joined\nregion="<<region->id()<<"\nuser_id="<<user_id<<"\navatar_id="<<avatar_id<<"\nsequence="<<region->latest_sequence()<<"\nterrain_revision="<<region->terrain().revision()<<'\n';
        socket.send_frame({protocol::MessageType::scene_join_ack,join.request_id,protocol::payload_from_string(joined.str())});
        while(true){const auto frame=socket.receive_frame();const auto b=protocol::payload_as_string(frame);
            if(frame.type==protocol::MessageType::scene_snapshot_request)socket.send_frame({protocol::MessageType::scene_snapshot,frame.request_id,protocol::payload_from_string(serialize_snapshot(*region))});
            else if(frame.type==protocol::MessageType::scene_events_request)socket.send_frame({protocol::MessageType::scene_events,frame.request_id,protocol::payload_from_string(serialize_events(*region,integer(b,"since")))});
            else if(frame.type==protocol::MessageType::entity_create){auto t=transform_from_payload(b);auto name=clean(field(b,"name"));const bool physical=field(b,"physical")!="false";const auto id=region->spawn_object(name.empty()?"Object":name,t,physical,user_id);socket.send_frame({protocol::MessageType::entity_create_ack,frame.request_id,protocol::payload_from_string("status=created\nid="+std::to_string(id)+"\n")});}
            else if(frame.type==protocol::MessageType::entity_update){const auto id=integer(b,"id");const auto current=region->entity(id);const bool owned=current&&current->kind==EntityKind::object&&current->owner_user_id==user_id;const bool ok=owned&&region->update_transform(id,transform_from_payload(b,current->transform));socket.send_frame({ok?protocol::MessageType::entity_update_ack:protocol::MessageType::error,frame.request_id,protocol::payload_from_string(ok?"status=updated\n":"reason=not-owner-or-unknown-entity\n")});}
            else if(frame.type==protocol::MessageType::entity_delete){const auto id=integer(b,"id");const auto current=region->entity(id);const bool owned=current&&current->kind==EntityKind::object&&current->owner_user_id==user_id;const bool ok=owned&&region->remove_entity(id);socket.send_frame({ok?protocol::MessageType::entity_delete_ack:protocol::MessageType::error,frame.request_id,protocol::payload_from_string(ok?"status=deleted\n":"reason=not-owner-or-unknown-entity\n")});}
            else if(frame.type==protocol::MessageType::chat_send){const auto seq=region->chat(avatar_id,field(b,"text"));socket.send_frame({seq?protocol::MessageType::chat_event:protocol::MessageType::error,frame.request_id,protocol::payload_from_string(seq?"status=sent\nsequence="+std::to_string(seq)+"\n":"reason=chat-rejected\n")});}
            else if(frame.type==protocol::MessageType::terrain_sample_request){double x=number(b,"x",0),y=number(b,"y",0);std::ostringstream sample;sample<<std::fixed<<std::setprecision(3)<<"x="<<x<<"\ny="<<y<<"\nheight="<<region->terrain().sample(x,y)<<"\nrevision="<<region->terrain().revision()<<'\n';socket.send_frame({protocol::MessageType::terrain_sample,frame.request_id,protocol::payload_from_string(sample.str())});}
            else if(frame.type==protocol::MessageType::terrain_set_request){auto x=integer(b,"x"),y=integer(b,"y");double h=number(b,"height",region->terrain().base_height());bool ok=region->set_terrain_height((size_t)x,(size_t)y,h);socket.send_frame({ok?protocol::MessageType::terrain_set_ack:protocol::MessageType::error,frame.request_id,protocol::payload_from_string(ok?"status=updated\nrevision="+std::to_string(region->terrain().revision())+"\n":"reason=terrain-update-rejected\n")});}
            else if(frame.type==protocol::MessageType::ping)socket.send_frame({protocol::MessageType::pong,frame.request_id,frame.payload});
            else if(frame.type==protocol::MessageType::goodbye)break;
            else socket.send_frame({protocol::MessageType::error,frame.request_id,protocol::payload_from_string("reason=unsupported-scene-message\n")});
        }
    }catch(const std::exception& e){common::log(common::LogLevel::debug,"world.scene.client",e.what());}
    if(region&&avatar_id)region->remove_entity(avatar_id);
}
} // namespace

SceneServer::SceneServer(std::string address,std::uint16_t port,const std::vector<std::shared_ptr<RegionRuntime>>& regions,std::string ticket_secret)
    :address_(std::move(address)),port_(port),auth_(std::make_shared<SceneAuthContext>(std::move(ticket_secret))){for(const auto&r:regions)regions_.emplace(r->id(),r);}
SceneServer::~SceneServer(){stop();}
void SceneServer::start(){if(running_.exchange(true))return;thread_=std::thread(&SceneServer::run,this);}
void SceneServer::stop(){if(!running_.exchange(false))return;if(thread_.joinable())thread_.join();}
void SceneServer::run(){try{opengenesis::network::TcpListener listener(address_,port_);common::log(common::LogLevel::info,"world.scene","Authenticated Scene endpoint listening on "+address_+":"+std::to_string(port_));while(running_){auto socket=listener.accept_for(std::chrono::milliseconds{250});if(!socket)continue;auto regions=regions_;auto auth=auth_;std::thread(handle_client,std::move(*socket),std::move(regions),std::move(auth)).detach();}}catch(const std::exception&e){if(running_)common::log(common::LogLevel::error,"world.scene",e.what());}}
} // namespace opengenesis::world
