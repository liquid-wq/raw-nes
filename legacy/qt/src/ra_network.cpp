#include "ra_network.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
QString quote_plus(const std::string& s) {
    QString out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') {
            out += QChar(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            out += QString("%%1").arg(static_cast<int>(c), 2, 16, QChar('0')).toUpper();
        }
    }
    return out;
}
} // namespace

QString ra_urlencode(const std::map<std::string, std::string>& params) {
    QStringList parts;
    for (const auto& kv : params) {
        parts << quote_plus(kv.first) + "=" + quote_plus(kv.second);
    }
    return parts.join("&");
}

QString ra_build_url(const std::map<std::string, std::string>& params) {
    return QString("https://%1/dorequest.php?%2").arg(RA_HOST).arg(ra_urlencode(params));
}

namespace {
Json qjson_to_json(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null: return Json::null();
        case QJsonValue::Bool: return Json::boolean(v.toBool());
        case QJsonValue::Double: {
            double d = v.toDouble();
            if (d == static_cast<int64_t>(d)) return Json::integer(static_cast<int64_t>(d));
            return Json::real(d);
        }
        case QJsonValue::String: return Json::string(v.toString().toStdString());
        case QJsonValue::Array: {
            Json arr = Json::array();
            for (const auto& item : v.toArray()) arr.push_back(qjson_to_json(item));
            return arr;
        }
        case QJsonValue::Object: {
            Json obj = Json::object();
            QJsonObject qo = v.toObject();
            for (auto it = qo.begin(); it != qo.end(); ++it)
                obj.set(it.key().toStdString(), qjson_to_json(it.value()));
            return obj;
        }
        default: return Json::null();
    }
}
} // namespace

RequestFn make_qt_request_fn() {
    return [](const std::map<std::string, std::string>& params) -> Json {
        static thread_local QNetworkAccessManager mgr; // Keep-Alive statt Neuaufbau pro Anfrage
        QUrl url(ra_build_url(params));
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QString(RA_USER_AGENT));

        QEventLoop loop;
        QNetworkReply* reply = mgr.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(12000, &loop, &QEventLoop::quit); // timeout=12 (wie Python)
        loop.exec();

        if (!reply->isFinished()) {
            reply->abort();
            reply->deleteLater();
            throw std::runtime_error("Zeitueberschreitung (12s) bei RA-Anfrage");
        }

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            QByteArray ra = reply->rawHeader("Retry-After");
            bool ok = false;
            int retry_after = ra.toInt(&ok);
            reply->deleteLater();
            throw RateLimited(ok ? retry_after : 0);
        }
        if (reply->error() != QNetworkReply::NoError && status != 200) {
            QString err = reply->errorString();
            reply->deleteLater();
            throw std::runtime_error(err.toStdString());
        }
        QByteArray body = reply->readAll();
        reply->deleteLater();

        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            throw std::runtime_error("ungueltige JSON-Antwort von RA");
        }
        return qjson_to_json(doc.object());
    };
}
