#include "wifimgr/profile_store.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

#include "wifimgr/log.hpp"
#include "wifimgr/strutil.hpp"

namespace wifimgr {
namespace {

bool writeAll(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

std::string dirName(const std::string& path) {
  auto slash = path.rfind('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

bool mkdirs(const std::string& dir) {
  if (dir.empty() || dir == "/" || dir == ".") return true;
  struct stat st;
  if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
  if (!mkdirs(dirName(dir))) return false;
  return mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST;
}

bool applyKey(Profile& p, std::string_view key, const std::string& v, std::string* err) {
  if (key == "ssid") p.ssid = v;
  else if (key == "security") {
    auto s = securityFromString(v);
    if (!s) {
      *err = "unknown security '" + v + "'";
      return false;
    }
    p.security = *s;
  } else if (key == "psk") p.psk = v;
  else if (key == "eap") p.eap = v;
  else if (key == "identity") p.identity = v;
  else if (key == "anonymous_identity") p.anonymousIdentity = v;
  else if (key == "password") p.password = v;
  else if (key == "phase2") p.phase2 = v;
  else if (key == "ca_cert") p.caCert = v;
  else if (key == "priority") {
    auto n = parseInt(v);
    if (!n || *n < 0 || *n > 1000) {
      *err = "priority must be 0..1000";
      return false;
    }
    p.priority = static_cast<int>(*n);
  } else if (key == "hidden") {
    if (!parseBool(v, &p.hidden)) {
      *err = "hidden must be a boolean";
      return false;
    }
  } else if (key == "autoconnect") {
    if (!parseBool(v, &p.autoconnect)) {
      *err = "autoconnect must be a boolean";
      return false;
    }
  } else {
    *err = "unknown key '" + std::string(key) + "'";
    return false;
  }
  return true;
}

}  // namespace

std::string ProfileStore::serialize(const std::vector<Profile>& profiles) {
  std::ostringstream o;
  o << "# Managed by wifimgrd. Contains secrets: keep mode 0600.\n";
  for (const auto& p : profiles) {
    o << "\n[network]\n";
    o << "ssid=" << cEscape(p.ssid) << "\n";
    o << "security=" << toString(p.security) << "\n";
    if (!p.psk.empty()) o << "psk=" << cEscape(p.psk) << "\n";
    if (p.security == Security::WpaEap) {
      o << "eap=" << cEscape(p.eap) << "\n";
      o << "identity=" << cEscape(p.identity) << "\n";
      if (!p.anonymousIdentity.empty())
        o << "anonymous_identity=" << cEscape(p.anonymousIdentity) << "\n";
      o << "password=" << cEscape(p.password) << "\n";
      o << "phase2=" << cEscape(p.phase2) << "\n";
      if (!p.caCert.empty()) o << "ca_cert=" << cEscape(p.caCert) << "\n";
    }
    o << "priority=" << p.priority << "\n";
    o << "hidden=" << (p.hidden ? "true" : "false") << "\n";
    o << "autoconnect=" << (p.autoconnect ? "true" : "false") << "\n";
  }
  return o.str();
}

std::vector<Profile> ProfileStore::deserialize(const std::string& text,
                                               std::vector<std::string>* warnings) {
  std::vector<Profile> out;
  Profile cur;
  bool inSection = false, sectionBad = false;
  int sectionLine = 0;

  auto finish = [&] {
    if (!inSection) return;
    if (sectionBad) {
      warnings->push_back("line " + std::to_string(sectionLine) + ": network skipped");
    } else if (auto e = cur.validate()) {
      warnings->push_back("line " + std::to_string(sectionLine) + ": network skipped: " + *e);
    } else {
      auto dup = std::find_if(out.begin(), out.end(),
                              [&](const Profile& p) { return p.ssid == cur.ssid; });
      if (dup != out.end()) *dup = cur;  // last definition wins
      else out.push_back(cur);
    }
  };

  int lineNo = 0;
  for (std::string_view raw : split(text, '\n')) {
    ++lineNo;
    std::string_view line = trim(raw);
    if (line.empty() || line[0] == '#') continue;
    if (line == "[network]") {
      finish();
      cur = Profile{};
      inSection = true;
      sectionBad = false;
      sectionLine = lineNo;
      continue;
    }
    auto eq = line.find('=');
    if (!inSection || eq == std::string_view::npos) {
      warnings->push_back("line " + std::to_string(lineNo) + ": ignored");
      continue;
    }
    // Do not trim the value: leading/trailing spaces are significant in
    // SSIDs and passphrases. Use the raw line after the '='.
    std::string_view key = trim(line.substr(0, eq));
    size_t rawEq = raw.find('=');
    std::string_view rawValue = raw.substr(rawEq + 1);
    if (!rawValue.empty() && rawValue.back() == '\r') rawValue.remove_suffix(1);
    auto value = cUnescape(rawValue);
    std::string err;
    if (!value) {
      err = "bad escape sequence";
    } else if (applyKey(cur, key, *value, &err)) {
      continue;
    }
    warnings->push_back("line " + std::to_string(lineNo) + ": " + err);
    sectionBad = true;
  }
  finish();
  return out;
}

bool ProfileStore::load(std::string* err) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    if (errno == ENOENT) {
      profiles_.clear();
      return true;
    }
    *err = path_ + ": " + std::strerror(errno);
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::vector<std::string> warnings;
  profiles_ = deserialize(ss.str(), &warnings);
  for (const auto& w : warnings) LOGW("%s: %s", path_.c_str(), w.c_str());

  struct stat st;
  if (stat(path_.c_str(), &st) == 0 && (st.st_mode & 077) != 0)
    LOGW("%s is accessible by other users (mode %03o); it contains secrets", path_.c_str(),
         st.st_mode & 0777);
  return true;
}

bool ProfileStore::save(std::string* err) const {
  std::string dir = dirName(path_);
  if (!mkdirs(dir)) {
    *err = "cannot create " + dir + ": " + std::strerror(errno);
    return false;
  }
  std::string tmp = path_ + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    *err = tmp + ": " + std::strerror(errno);
    return false;
  }
  fchmod(fd, 0600);  // in case the temp file pre-existed with other bits
  std::string data = serialize(profiles_);
  bool ok = writeAll(fd, data) && fsync(fd) == 0;
  int saved = errno;
  ::close(fd);
  if (!ok || rename(tmp.c_str(), path_.c_str()) != 0) {
    if (ok) saved = errno;
    unlink(tmp.c_str());
    *err = path_ + ": " + std::strerror(saved);
    return false;
  }
  int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    fsync(dfd);
    ::close(dfd);
  }
  return true;
}

const Profile* ProfileStore::find(const std::string& ssid) const {
  for (const auto& p : profiles_)
    if (p.ssid == ssid) return &p;
  return nullptr;
}

void ProfileStore::upsert(Profile p) {
  for (auto& existing : profiles_) {
    if (existing.ssid == p.ssid) {
      existing = std::move(p);
      return;
    }
  }
  profiles_.push_back(std::move(p));
}

bool ProfileStore::remove(const std::string& ssid) {
  auto it = std::remove_if(profiles_.begin(), profiles_.end(),
                           [&](const Profile& p) { return p.ssid == ssid; });
  if (it == profiles_.end()) return false;
  profiles_.erase(it, profiles_.end());
  return true;
}

}  // namespace wifimgr
