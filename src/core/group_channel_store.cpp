#include "opengenesis/core/group_channel_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace opengenesis::core {namespace {std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}}
GroupChannelStore::GroupChannelStore(std::string path):path_(std::move(path)){load();}
std::string GroupChannelStore::clean(std::string v,std::size_t max){for(char&c:v)if(c=='\t'||c=='\n'||c=='\r')c=' ';if(v.size()>max)v.resize(max);return v;}
std::optional<GroupPost> GroupChannelStore::send(std::string group,std::string sender,std::string kind,std::string title,std::string text,std::string& reason){if(group.empty()||sender.empty()||(kind!="chat"&&kind!="notice")){reason="invalid-group-post";return std::nullopt;}title=clean(std::move(title),160);text=clean(std::move(text),2000);if(text.empty()){reason="empty-message";return std::nullopt;}GroupPost p{.id=security::random_hex(16),.group_id=std::move(group),.sender_id=std::move(sender),.kind=std::move(kind),.title=std::move(title),.text=std::move(text),.sent_unix=now()};std::scoped_lock l(mutex_);posts_.push_back(p);if(posts_.size()>20000)posts_.erase(posts_.begin(),posts_.begin()+2000);persist_locked();reason.clear();return p;}
std::vector<GroupPost> GroupChannelStore::list(std::string_view group,std::size_t limit)const{std::scoped_lock l(mutex_);std::vector<GroupPost>o;for(auto it=posts_.rbegin();it!=posts_.rend()&&o.size()<limit;++it)if(it->group_id==group)o.push_back(*it);return o;}
std::size_t GroupChannelStore::count()const{std::scoped_lock l(mutex_);return posts_.size();}
void GroupChannelStore::load(){std::scoped_lock l(mutex_);posts_.clear();std::ifstream in(path_);std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);std::vector<std::string>f;std::string v;while(std::getline(s,v,'\t'))f.push_back(v);if(f.size()!=7)continue;try{posts_.push_back({.id=f[0],.group_id=f[1],.sender_id=f[2],.kind=f[3],.title=f[4],.text=f[5],.sent_unix=std::stoll(f[6])});}catch(...){}}}
void GroupChannelStore::persist_locked()const{std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto t=p.string()+".tmp";std::ofstream out(t,std::ios::trunc);if(!out)throw std::runtime_error("cannot write group channels");out<<"# OpenGenesisLINK group channels v1\n";for(auto&post:posts_)out<<post.id<<'\t'<<post.group_id<<'\t'<<post.sender_id<<'\t'<<post.kind<<'\t'<<post.title<<'\t'<<post.text<<'\t'<<post.sent_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush group channels");opengenesis::platform::replace_file(t,p);}
}
