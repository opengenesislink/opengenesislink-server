#include "opengenesis/core/moderation_store.hpp"
#include "opengenesis/security/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace opengenesis::core { namespace {
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string hex(std::string_view v){constexpr char h[]="0123456789abcdef";std::string o(v.size()*2,'0');for(size_t i=0;i<v.size();++i){auto c=(unsigned char)v[i];o[i*2]=h[c>>4];o[i*2+1]=h[c&15];}return o;}
unsigned char nib(char c){if(c>='0'&&c<='9')return static_cast<unsigned char>(c-'0');if(c>='a'&&c<='f')return static_cast<unsigned char>(c-'a'+10);if(c>='A'&&c<='F')return static_cast<unsigned char>(c-'A'+10);throw std::runtime_error("hex");}
std::string unhex(std::string_view v){if(v.size()%2)throw std::runtime_error("hex");std::string o(v.size()/2,'\0');for(size_t i=0;i<o.size();++i)o[i]=(char)((nib(v[i*2])<<4)|nib(v[i*2+1]));return o;}
std::vector<std::string> tabs(const std::string& l){std::vector<std::string>f;size_t s=0;for(;;){auto e=l.find('\t',s);f.push_back(l.substr(s,e==std::string::npos?e:e-s));if(e==std::string::npos)break;s=e+1;}return f;}
}
ModerationStore::ModerationStore(std::string path):path_(std::move(path)){reload();}
void ModerationStore::reload(){std::scoped_lock l(mutex_);load_locked();}
std::optional<BanRecord> ModerationStore::ban(std::string actor,std::string user,std::string scope,std::string scope_id,std::string reason,std::int64_t expires,std::string& error){if(user.empty()||(scope!="global"&&scope!="region")||(scope=="region"&&scope_id.empty())){error="invalid-ban";return std::nullopt;}BanRecord b{.id=security::random_hex(16),.user_id=std::move(user),.scope=std::move(scope),.scope_id=std::move(scope_id),.reason=std::move(reason),.created_by=std::move(actor),.created_unix=now(),.expires_unix=expires};std::scoped_lock l(mutex_);bans_[b.id]=b;persist_locked();error.clear();return b;}
bool ModerationStore::unban(std::string_view id){std::scoped_lock l(mutex_);if(!bans_.erase(std::string{id}))return false;persist_locked();return true;}
bool ModerationStore::is_banned(std::string_view u,std::string_view region)const{auto t=now();std::scoped_lock l(mutex_);for(auto&[_,b]:bans_)if(b.user_id==u&&(b.expires_unix==0||b.expires_unix>t)&&(b.scope=="global"||(b.scope=="region"&&b.scope_id==region)))return true;return false;}
std::vector<BanRecord> ModerationStore::list()const{std::scoped_lock l(mutex_);std::vector<BanRecord>r;for(auto&[_,b]:bans_)r.push_back(b);std::sort(r.begin(),r.end(),[](auto&a,auto&b){return a.created_unix>b.created_unix;});return r;}
std::size_t ModerationStore::active_count()const{auto t=now();std::scoped_lock l(mutex_);return static_cast<std::size_t>(std::count_if(bans_.begin(),bans_.end(),[&](auto&e){return e.second.expires_unix==0||e.second.expires_unix>t;}));}
void ModerationStore::load_locked(){bans_.clear();std::ifstream in(path_);if(!in)return;std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;auto f=tabs(line);if(f.size()!=8 && f.size()!=9)continue;try{BanRecord b{.id=f[0],.user_id=f[1],.scope=f[2],.scope_id=f[3],.reason=unhex(f[4]),.created_by=f[5],.created_unix=std::stoll(f[6]),.expires_unix=std::stoll(f[7])};bans_[b.id]=b;}catch(...){}}}
void ModerationStore::persist_locked()const{std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto t=p.string()+".tmp";std::ofstream out(t,std::ios::trunc);if(!out)throw std::runtime_error("cannot write moderation");out<<"# OpenGenesisLINK moderation v1\n";for(auto&[_,b]:bans_)out<<b.id<<'\t'<<b.user_id<<'\t'<<b.scope<<'\t'<<b.scope_id<<'\t'<<hex(b.reason)<<'\t'<<b.created_by<<'\t'<<b.created_unix<<'\t'<<b.expires_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush moderation");std::filesystem::rename(t,p);}
}
