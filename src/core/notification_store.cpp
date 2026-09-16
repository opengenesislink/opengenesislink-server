#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/security/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace opengenesis::core {namespace {std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}}
NotificationStore::NotificationStore(std::string path):path_(std::move(path)){load();}
std::string NotificationStore::clean(std::string v,std::size_t max){for(char&c:v)if(c=='\t'||c=='\r'||c=='\n')c=' ';if(v.size()>max)v.resize(max);return v;}
NotificationInfo NotificationStore::push(std::string user,std::string type,std::string title,std::string body,std::string target){NotificationInfo n{.id=security::random_hex(16),.user_id=std::move(user),.type=clean(std::move(type),48),.title=clean(std::move(title),160),.body=clean(std::move(body),2000),.target=clean(std::move(target),256),.created_unix=now()};std::scoped_lock l(mutex_);items_.push_back(n);if(items_.size()>10000)items_.erase(items_.begin(),items_.begin()+1000);persist_locked();return n;}
std::vector<NotificationInfo> NotificationStore::list_for_user(std::string_view user,std::size_t limit)const{std::scoped_lock l(mutex_);std::vector<NotificationInfo>o;for(auto it=items_.rbegin();it!=items_.rend()&&o.size()<limit;++it)if(it->user_id==user)o.push_back(*it);return o;}
bool NotificationStore::mark_read(std::string_view user,std::string_view id){std::scoped_lock l(mutex_);for(auto&n:items_)if(n.user_id==user&&n.id==id){if(n.read_unix==0){n.read_unix=now();persist_locked();}return true;}return false;}
std::size_t NotificationStore::unread_count(std::string_view user)const{std::scoped_lock l(mutex_);return static_cast<std::size_t>(std::count_if(items_.begin(),items_.end(),[&](auto&n){return n.user_id==user&&n.read_unix==0;}));}
std::size_t NotificationStore::count()const{std::scoped_lock l(mutex_);return items_.size();}
void NotificationStore::load(){std::scoped_lock l(mutex_);items_.clear();std::ifstream in(path_);std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);std::vector<std::string>f;std::string v;while(std::getline(s,v,'\t'))f.push_back(v);if(f.size()!=8)continue;try{items_.push_back({.id=f[0],.user_id=f[1],.type=f[2],.title=f[3],.body=f[4],.target=f[5],.created_unix=std::stoll(f[6]),.read_unix=std::stoll(f[7])});}catch(...){}}}
void NotificationStore::persist_locked()const{std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto t=p.string()+".tmp";std::ofstream out(t,std::ios::trunc);if(!out)throw std::runtime_error("cannot write notifications");out<<"# OpenGenesisLINK notifications v1\n";for(auto&n:items_)out<<n.id<<'\t'<<n.user_id<<'\t'<<n.type<<'\t'<<n.title<<'\t'<<n.body<<'\t'<<n.target<<'\t'<<n.created_unix<<'\t'<<n.read_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush notifications");std::filesystem::rename(t,p);}
}
