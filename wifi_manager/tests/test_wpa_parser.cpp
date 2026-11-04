#include "test.hpp"
#include "wifimgr/supplicant.hpp"
#include "wifimgr/wpa_parser.hpp"

using namespace wifimgr;

TEST(security_from_flags) {
  CHECK(securityFromFlags("[WPA2-PSK-CCMP][ESS]") == Security::WpaPsk);
  CHECK(securityFromFlags("[WPA-PSK-TKIP][WPA2-PSK-CCMP][WPS][ESS]") == Security::WpaPsk);
  CHECK(securityFromFlags("[RSN-SAE-CCMP][ESS]") == Security::Sae);
  CHECK(securityFromFlags("[WPA2-PSK+SAE-CCMP][ESS]") == Security::WpaPskSae);
  CHECK(securityFromFlags("[WPA2-EAP-CCMP][ESS]") == Security::WpaEap);
  CHECK(securityFromFlags("[WEP][ESS]") == Security::Wep);
  CHECK(securityFromFlags("[WPA2-OWE-CCMP][ESS]") == Security::Unknown);
  CHECK(securityFromFlags("[ESS]") == Security::Open);
  CHECK(securityFromFlags("") == Security::Open);
}

TEST(parse_scan_results) {
  const char* reply =
      "bssid / frequency / signal level / flags / ssid\n"
      "00:11:22:33:44:55\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\tDepot\n"
      "00:11:22:33:44:66\t5180\t-61\t[RSN-SAE-CCMP][ESS]\tCaf\\xc3\\xa9 \\\"5G\\\"\n"
      "00:11:22:33:44:77\t2412\t-80\t[ESS]\t\n"
      "garbage line\n";
  auto r = parseScanResults(reply);
  REQUIRE(r.size() == 3);
  CHECK_EQ(r[0].ssid, std::string("Depot"));
  CHECK_EQ(r[0].frequency, 2437);
  CHECK_EQ(r[0].rssi, -48);
  CHECK(r[0].security == Security::WpaPsk);
  CHECK_EQ(r[1].ssid, std::string("Caf\xc3\xa9 \"5G\""));
  CHECK(r[1].security == Security::Sae);
  CHECK(r[2].ssid.empty());
  CHECK(r[2].security == Security::Open);
}

TEST(parse_key_values) {
  auto kv = parseKeyValues("wpa_state=COMPLETED\nssid=Depot\nip_address=10.0.0.5\nbogus\n");
  CHECK_EQ(kv["wpa_state"], std::string("COMPLETED"));
  CHECK_EQ(kv["ip_address"], std::string("10.0.0.5"));
  CHECK_EQ(kv.size(), size_t(3));
}

TEST(parse_events) {
  using T = SupplicantEvent::Type;
  auto e = parseEvent("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:55 completed [id=0 id_str=]");
  CHECK(e.type == T::Connected);
  CHECK_EQ(e.bssid, std::string("00:11:22:33:44:55"));

  e = parseEvent("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=3 locally_generated=1");
  CHECK(e.type == T::Disconnected);
  CHECK_EQ(e.reason, std::string("3"));
  CHECK(e.locallyGenerated);

  e = parseEvent("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"My \\\"Net\\\"\" auth_failures=1 duration=10 reason=WRONG_KEY");
  CHECK(e.type == T::AuthFailure);
  CHECK_EQ(e.ssid, std::string("My \"Net\""));
  CHECK_EQ(e.reason, std::string("WRONG_KEY"));

  e = parseEvent("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"x\" auth_failures=1 duration=10 reason=CONN_FAILED");
  CHECK(e.type == T::ConnFailed);

  CHECK(parseEvent("<3>CTRL-EVENT-SCAN-RESULTS ").type == T::ScanResults);
  CHECK(parseEvent("<3>CTRL-EVENT-SCAN-FAILED ret=-16").type == T::ScanFailed);
  CHECK(parseEvent("<3>CTRL-EVENT-ASSOC-REJECT bssid=00:11:22:33:44:55 status_code=17").type == T::ConnFailed);
  CHECK(parseEvent("<3>CTRL-EVENT-NETWORK-NOT-FOUND").type == T::NetworkNotFound);
  CHECK(parseEvent("<3>CTRL-EVENT-EAP-FAILURE EAP authentication failed").type == T::AuthFailure);
  CHECK(parseEvent("<2>CTRL-EVENT-TERMINATING").type == T::Terminating);
  CHECK(parseEvent("<3>WPS-AP-AVAILABLE").type == T::Other);
}

TEST(event_field_matches_whole_words_only) {
  CHECK_EQ(eventField("x bssid=aa reason=3", "reason"), std::string("3"));
  CHECK_EQ(eventField("x old_reason=1 reason=3", "reason"), std::string("3"));
  CHECK_EQ(eventField("x reason=3", "missing"), std::string());
}

TEST(wpa_string_value_quotes_or_hex_encodes) {
  CHECK_EQ(wpaStringValue("user@corp"), std::string("\"user@corp\""));
  CHECK_EQ(wpaStringValue("a\"b"), std::string("612262"));
  CHECK_EQ(wpaStringValue("\xc3\xa9"), std::string("c3a9"));
}
