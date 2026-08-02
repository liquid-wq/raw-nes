#pragma once
#include "ra_client.h"
#include <QString>
#include <map>

constexpr const char* RA_HOST = "retroachievements.org";
constexpr const char* RA_USER_AGENT = "RAW-NES-CPP/1.0";

QString ra_urlencode(const std::map<std::string, std::string>& params);
QString ra_build_url(const std::map<std::string, std::string>& params);

// Synchroner RequestFn-Adapter fuer RaClient (siehe ra_client.h), nutzt
// QNetworkAccessManager + QEventLoop. Wirft RateLimited bei HTTP 429.
// 1:1 Muster aus MEGA-RAWs ra_network.h/cpp uebernommen.
RequestFn make_qt_request_fn();
