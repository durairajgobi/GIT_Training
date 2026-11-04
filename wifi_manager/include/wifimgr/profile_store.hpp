// Persistent store of saved networks.
//
// File format (one [network] section per profile, values C-escaped):
//   [network]
//   ssid=Depot WiFi
//   security=wpa-psk
//   psk=correct horse
//   priority=10
//
// The file holds secrets: it is written with mode 0600, atomically
// (temp file + fsync + rename + directory fsync) so a power cut during a
// save leaves either the old or the new file, never a truncated one.
#pragma once

#include <string>
#include <vector>

#include "wifimgr/types.hpp"

namespace wifimgr {

class ProfileStore {
 public:
  explicit ProfileStore(std::string path) : path_(std::move(path)) {}

  // Missing file is not an error (empty store). Invalid sections are skipped
  // with a warning so one bad entry cannot lock the device out of Wi-Fi.
  bool load(std::string* err);
  bool save(std::string* err) const;

  const std::string& path() const { return path_; }
  const std::vector<Profile>& profiles() const { return profiles_; }
  const Profile* find(const std::string& ssid) const;
  void upsert(Profile p);
  bool remove(const std::string& ssid);

  static std::string serialize(const std::vector<Profile>& profiles);
  static std::vector<Profile> deserialize(const std::string& text,
                                          std::vector<std::string>* warnings);

 private:
  std::string path_;
  std::vector<Profile> profiles_;
};

}  // namespace wifimgr
