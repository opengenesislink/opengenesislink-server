#include "opengenesis/core/audit_store.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace opengenesis::core { namespace {
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string clean(std::string v){for(char&c:v)if(c=='\t'||c=='\n'||c=='\r')c=' ';if(v.size()>512)v.resize(512);return v;}
}
AuditStore::AuditStore(std::string path):path_(std::move(path)){load();}
void AuditStore::append(std::string actor,std::string action,std::string target,std::string detail){std::scoped_lock l(mutex_);AuditEvent e{.sequence=next_++,.actor=clean(std::move(actor)),.action=clean(std::move(action)),.target=clean(std::move(target)),.detail=clean(std::move(detail)),.unix_time=now()};events_.push_back(e);std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream out(path_,std::ios::app);out<<e.sequence<<'\t'<<e.unix_time<<'\t'<<e.actor<<'\t'<<e.action<<'\t'<<e.target<<'\t'<<e.detail<<'\n';}
std::vector<AuditEvent> AuditStore::recent(std::size_t limit)const{std::scoped_lock l(mutex_);limit=std::min(limit,events_.size());return std::vector<AuditEvent>(events_.end()-static_cast<std::ptrdiff_t>(limit),events_.end());}
std::size_t AuditStore::count()const{std::scoped_lock l(mutex_);return events_.size();}
void AuditStore::load(){std::scoped_lock l(mutex_);events_.clear();std::ifstream in(path_);std::string line;while(std::getline(in,line)){std::istringstream s(line);AuditEvent e;std::string seq,time;if(!std::getline(s,seq,'\t')||!std::getline(s,time,'\t')||!std::getline(s,e.actor,'\t')||!std::getline(s,e.action,'\t')||!std::getline(s,e.target,'\t')||!std::getline(s,e.detail))continue;try{e.sequence=std::stoull(seq);e.unix_time=std::stoll(time);events_.push_back(e);next_=std::max(next_,e.sequence+1);}catch(...){}}}
}
