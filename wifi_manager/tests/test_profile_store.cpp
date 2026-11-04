#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>

#include "test.hpp"
#include "wifimgr/profile_store.hpp"

using namespace wifimgr;

namespace {

std::string tempDir() {
  char tmpl[] = "/tmp/wifimgr-test-XXXXXX";
  const char* d = mkdtemp(tmpl);
  return d ? d : "/tmp";
}

Profile psk(const std::string& ssid, const std::string& pass, int prio = 0) {
  Profile p;
  p.ssid = ssid;
  p.security = Security::WpaPsk;
  p.psk = pass;
  p.priority = prio;
  return p;
}

}  // namespace

TEST(profile_validation) {
  Profile p = psk("Net", "short");
  CHECK(p.validate().has_value());
  p.psk = "longenough";
  CHECK(!p.validate().has_value());
  p.psk = std::string(64, 'a');
  CHECK(!p.validate().has_value());
  p.psk = std::string(64, 'z');  // 64 chars but not hex
  CHECK(p.validate().has_value());
  p.psk = "with\nnewline";
  CHECK(p.validate().has_value());

  Profile open;
  open.ssid = "Open";
  open.security = Security::Open;
  CHECK(!open.validate().has_value());
  open.psk = "oops-a-key";
  CHECK(open.validate().has_value());

  Profile longSsid = psk(std::string(33, 'x'), "password1");
  CHECK(longSsid.validate().has_value());

  Profile eap;
  eap.ssid = "Corp";
  eap.security = Security::WpaEap;
  CHECK(eap.validate().has_value());  // no identity
  eap.identity = "me";
  eap.password = "pw";
  CHECK(!eap.validate().has_value());
  eap.eap = "MD5";
  CHECK(eap.validate().has_value());
}

TEST(serialize_round_trip_with_awkward_bytes) {
  Profile a = psk(" lead/trail space ", "pa=ss#word\\x", 7);
  a.hidden = true;
  Profile b;
  b.ssid = std::string("bin\x01\xff\n", 6);
  b.security = Security::Open;
  b.autoconnect = false;
  Profile c;
  c.ssid = "Corp";
  c.security = Security::WpaEap;
  c.identity = "user@corp";
  c.password = "secret";
  c.caCert = "/etc/ssl/corp.pem";

  std::string text = ProfileStore::serialize({a, b, c});
  std::vector<std::string> warnings;
  auto back = ProfileStore::deserialize(text, &warnings);
  CHECK(warnings.empty());
  REQUIRE(back.size() == 3);
  CHECK_EQ(back[0].ssid, a.ssid);
  CHECK_EQ(back[0].psk, a.psk);
  CHECK_EQ(back[0].priority, 7);
  CHECK(back[0].hidden);
  CHECK(back[1].ssid == b.ssid);
  CHECK(!back[1].autoconnect);
  CHECK(back[2].security == Security::WpaEap);
  CHECK_EQ(back[2].caCert, c.caCert);
}

TEST(deserialize_skips_invalid_sections_and_keeps_good_ones) {
  const char* text =
      "orphan=1\n"
      "[network]\nssid=Good\nsecurity=wpa-psk\npsk=password123\n"
      "[network]\nssid=BadKey\nsecurity=wpa-psk\npsk=short\n"
      "[network]\nssid=Unknown\nsecurity=wpa-psk\npsk=password123\ncolour=blue\n"
      "[network]\nssid=Good\nsecurity=open\n";  // duplicate: last wins
  std::vector<std::string> warnings;
  auto ps = ProfileStore::deserialize(text, &warnings);
  REQUIRE(ps.size() == 1);
  CHECK_EQ(ps[0].ssid, std::string("Good"));
  CHECK(ps[0].security == Security::Open);
  CHECK(warnings.size() >= 3);
}

TEST(save_is_private_and_atomic_and_load_round_trips) {
  std::string dir = tempDir();
  std::string path = dir + "/sub/networks.conf";
  ProfileStore s(path);
  std::string err;
  REQUIRE(s.load(&err));  // missing file = empty store
  CHECK(s.profiles().empty());
  s.upsert(psk("Home", "password123", 3));
  s.upsert(psk("Work", "password456"));
  REQUIRE(s.save(&err));

  struct stat st;
  REQUIRE(stat(path.c_str(), &st) == 0);
  CHECK_EQ(st.st_mode & 0777, 0600u);
  CHECK(access((path + ".tmp").c_str(), F_OK) != 0);

  s.upsert(psk("Home", "newpassword", 9));  // update in place
  CHECK(s.remove("Work"));
  CHECK(!s.remove("Nope"));
  REQUIRE(s.save(&err));

  ProfileStore again(path);
  REQUIRE(again.load(&err));
  REQUIRE(again.profiles().size() == 1);
  CHECK_EQ(again.find("Home")->psk, std::string("newpassword"));
  CHECK_EQ(again.find("Home")->priority, 9);
  CHECK(std::system(("rm -rf " + dir).c_str()) == 0);
}

TEST(save_reports_unwritable_location) {
  ProfileStore s("/proc/wifimgr-cannot-write/networks.conf");
  s.upsert(psk("Home", "password123"));
  std::string err;
  CHECK(!s.save(&err));
  CHECK(!err.empty());
}
