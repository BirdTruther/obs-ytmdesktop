#include "ytmd-client.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace {
constexpr char kAppId[] = "obs-ytmdesktop";
constexpr char kAppName[] = "OBS YTMDesktop Sync";
constexpr char kAppVersion[] = "1.0.0";
constexpr int kPollIntervalMs = 3000;
constexpr int kMaxBackoffMs = 10000;
#ifdef _WIN32
constexpr int kMaxThumbnailBytes = 5 * 1024 * 1024;
#endif

#ifdef _WIN32
QByteArray downloadBytesWinHttp(const QString &urlStr)
{
	QByteArray result;
	const std::wstring wUrl = urlStr.toStdWString();

	URL_COMPONENTS uc = {};
	uc.dwStructSize = sizeof(uc);
	wchar_t hostBuf[256] = {};
	wchar_t pathBuf[2048] = {};
	uc.lpszHostName = hostBuf;
	uc.dwHostNameLength = _countof(hostBuf);
	uc.lpszUrlPath = pathBuf;
	uc.dwUrlPathLength = _countof(pathBuf);

	if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &uc)) {
		return result;
	}
	if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP) {
		return result;
	}

	HINTERNET hSession = WinHttpOpen(L"obs-ytmdesktop/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		return result;
	}
	WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 5000);

	HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, uc.nPort, 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return result;
	}

	const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathBuf, nullptr,
						WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (hRequest) {
		if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		    WinHttpReceiveResponse(hRequest, nullptr)) {
			DWORD avail = 0;
			while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
				if (avail > static_cast<DWORD>(kMaxThumbnailBytes) ||
				    result.size() + static_cast<int>(avail) > kMaxThumbnailBytes) {
					result.clear();
					break;
				}
				const int oldSize = result.size();
				result.resize(oldSize + static_cast<int>(avail));
				DWORD read = 0;
				if (!WinHttpReadData(hRequest, result.data() + oldSize, avail, &read)) {
					result.clear();
					break;
				}
				result.resize(oldSize + static_cast<int>(read));
			}
		}
		WinHttpCloseHandle(hRequest);
	}

	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return result;
}
#endif
} // namespace

YtmdWorker::YtmdWorker(QObject *parent) : QObject(parent) {}

YtmdWorker::~YtmdWorker() = default;

void YtmdWorker::start()
{
	if (!m_nam) {
		m_nam = new QNetworkAccessManager(this);
	}
	if (!m_timer) {
		m_timer = new QTimer(this);
		m_timer->setInterval(kPollIntervalMs);
		connect(m_timer, &QTimer::timeout, this, &YtmdWorker::pollState);
	}
	m_timer->start();
	pollState();
}

void YtmdWorker::stop()
{
	if (m_timer) {
		m_timer->stop();
	}
}

void YtmdWorker::setConfig(const QString &host, int port, const QString &token)
{
	m_host = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
	m_port = (port > 0 && port <= 65535) ? port : 9863;
	m_token = token.trimmed();
	if (m_timer && m_timer->isActive()) {
		pollState();
	}
}

QString YtmdWorker::baseUrl() const
{
	return QStringLiteral("http://%1:%2/api/v1").arg(m_host).arg(m_port);
}

void YtmdWorker::pollState()
{
	if (!m_nam || m_pollInFlight) {
		return;
	}

	m_pollInFlight = true;
	QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/state")));
	req.setTransferTimeout(2000);
	if (!m_token.isEmpty()) {
		req.setRawHeader("Authorization", m_token.toUtf8());
	}

	QNetworkReply *reply = m_nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		m_pollInFlight = false;
		reply->deleteLater();

		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError) {
			if (httpStatus == 429) {
				// Rate-limited: back off quietly without clearing current track
				if (m_timer) {
					const int nextMs = std::min(m_timer->interval() + 2000, kMaxBackoffMs);
					m_timer->setInterval(nextMs);
				}
				return;
			}

			m_consecutiveErrors++;
			if (httpStatus == 401 || httpStatus == 403) {
				emit statusChanged(QStringLiteral("Unauthorized — click 'Pair with YTMDesktop'"));
				YtmdTrackInfo empty;
				empty.valid = false;
				emit stateUpdated(empty, QDateTime::currentMSecsSinceEpoch());
			} else if (m_consecutiveErrors >= 3) {
				emit statusChanged(QStringLiteral("Disconnected (%1)").arg(reply->errorString()));
				YtmdTrackInfo empty;
				empty.valid = false;
				emit stateUpdated(empty, QDateTime::currentMSecsSinceEpoch());
			}
			return;
		}

		m_consecutiveErrors = 0;
		if (m_timer && m_timer->interval() != kPollIntervalMs) {
			m_timer->setInterval(kPollIntervalMs);
		}

		const QByteArray body = reply->readAll();
		QJsonParseError parseErr;
		const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
		if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
			emit statusChanged(QStringLiteral("Invalid JSON from YTMDesktop"));
			return;
		}

		const QJsonObject root = doc.object();
		const QJsonObject player = root.value(QStringLiteral("player")).toObject();
		const QJsonObject video = root.value(QStringLiteral("video")).toObject();

		YtmdTrackInfo info;
		info.trackState = player.value(QStringLiteral("trackState")).toInt(-1);
		info.videoProgress = player.value(QStringLiteral("videoProgress")).toDouble(0.0);
		info.videoId = video.value(QStringLiteral("id")).toString();
		info.title = video.value(QStringLiteral("title")).toString();
		info.author = video.value(QStringLiteral("author")).toString();
		info.album = video.value(QStringLiteral("album")).toString();
		info.durationSeconds = video.value(QStringLiteral("durationSeconds")).toDouble(0.0);
		info.valid = !info.title.isEmpty();

		QString bestThumb;
		int bestWidth = -1;
		const QJsonArray thumbs = video.value(QStringLiteral("thumbnails")).toArray();
		for (const QJsonValue &val : thumbs) {
			const QJsonObject t = val.toObject();
			const int w = t.value(QStringLiteral("width")).toInt(0);
			const QString u = t.value(QStringLiteral("url")).toString();
			if (!u.isEmpty() && w >= bestWidth) {
				bestWidth = w;
				bestThumb = u;
			}
		}
		info.thumbnailUrl = bestThumb;

		emit statusChanged(QStringLiteral("Connected"));
		emit stateUpdated(info, QDateTime::currentMSecsSinceEpoch());

		if (!bestThumb.isEmpty() && bestThumb != m_lastThumbnailUrl) {
			fetchThumbnail(bestThumb);
		}
	});
}

void YtmdWorker::fetchThumbnail(const QString &url)
{
	m_lastThumbnailUrl = url;
#ifdef _WIN32
	const QByteArray data = downloadBytesWinHttp(url);
	if (!data.isEmpty()) {
		QImage img;
		if (img.loadFromData(data)) {
			emit albumArtUpdated(url, img.convertToFormat(QImage::Format_RGBA8888));
		}
	}
#else
	if (!m_nam) {
		return;
	}
	QNetworkRequest req{QUrl(url)};
	req.setTransferTimeout(5000);
	QNetworkReply *reply = m_nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const QByteArray data = reply->readAll();
		QImage img;
		if (img.loadFromData(data)) {
			emit albumArtUpdated(url, img.convertToFormat(QImage::Format_RGBA8888));
		}
	});
#endif
}

void YtmdWorker::startPairing()
{
	if (!m_nam || m_pairingInFlight) {
		return;
	}
	m_pairingInFlight = true;
	emit statusChanged(QStringLiteral("Requesting pairing code..."));

	QJsonObject payload;
	payload.insert(QStringLiteral("appId"), QString::fromLatin1(kAppId));
	payload.insert(QStringLiteral("appName"), QString::fromLatin1(kAppName));
	payload.insert(QStringLiteral("appVersion"), QString::fromLatin1(kAppVersion));

	QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/auth/requestcode")));
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	req.setTransferTimeout(5000);

	QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			m_pairingInFlight = false;
			emit statusChanged(QStringLiteral("Pairing failed: %1").arg(reply->errorString()));
			return;
		}

		const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		const QString code = doc.object().value(QStringLiteral("code")).toString();
		if (code.isEmpty()) {
			m_pairingInFlight = false;
			emit statusChanged(QStringLiteral("Pairing failed: no code returned"));
			return;
		}

		emit statusChanged(QStringLiteral("Code: %1 — Approve in YTMDesktop!").arg(code));

		QJsonObject authPayload;
		authPayload.insert(QStringLiteral("appId"), QString::fromLatin1(kAppId));
		authPayload.insert(QStringLiteral("code"), code);

		QNetworkRequest authReq(QUrl(baseUrl() + QStringLiteral("/auth/request")));
		authReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		authReq.setTransferTimeout(35000);

		QNetworkReply *authReply = m_nam->post(authReq, QJsonDocument(authPayload).toJson(QJsonDocument::Compact));
		connect(authReply, &QNetworkReply::finished, this, [this, authReply]() {
			m_pairingInFlight = false;
			authReply->deleteLater();

			if (authReply->error() != QNetworkReply::NoError) {
				emit statusChanged(QStringLiteral("Pairing denied or timed out"));
				return;
			}

			const QJsonDocument authDoc = QJsonDocument::fromJson(authReply->readAll());
			const QString token = authDoc.object().value(QStringLiteral("token")).toString();
			if (token.isEmpty()) {
				emit statusChanged(QStringLiteral("Pairing failed: empty token"));
				return;
			}

			m_token = token;
			emit tokenReceived(token);
			emit statusChanged(QStringLiteral("Paired & Connected"));
			pollState();
		});
	});
}

YtmdClient::YtmdClient(QObject *parent) : QObject(parent)
{
	m_worker = new YtmdWorker();
	m_worker->moveToThread(&m_thread);

	connect(&m_thread, &QThread::started, m_worker, &YtmdWorker::start);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	connect(m_worker, &YtmdWorker::stateUpdated, this, &YtmdClient::onStateUpdated, Qt::QueuedConnection);
	connect(m_worker, &YtmdWorker::albumArtUpdated, this, &YtmdClient::onAlbumArtUpdated, Qt::QueuedConnection);
	connect(m_worker, &YtmdWorker::statusChanged, this, &YtmdClient::onStatusChanged, Qt::QueuedConnection);
	connect(m_worker, &YtmdWorker::tokenReceived, this, &YtmdClient::onTokenReceived, Qt::QueuedConnection);

	m_thread.start();
}

YtmdClient::~YtmdClient()
{
	if (m_worker) {
		QMetaObject::invokeMethod(m_worker, &YtmdWorker::stop, Qt::BlockingQueuedConnection);
	}
	m_thread.quit();
	m_thread.wait();
}

void YtmdClient::setConfig(const QString &host, int port, const QString &token)
{
	{
		QMutexLocker lock(&m_mutex);
		m_snapshot.token = token;
	}
	QMetaObject::invokeMethod(
		m_worker,
		[w = m_worker, host, port, token]() { w->setConfig(host, port, token); },
		Qt::QueuedConnection);
}

void YtmdClient::requestPairing()
{
	QMetaObject::invokeMethod(m_worker, &YtmdWorker::startPairing, Qt::QueuedConnection);
}

void YtmdClient::setTokenCallback(std::function<void(const QString &)> cb)
{
	QMutexLocker lock(&m_mutex);
	m_tokenCallback = std::move(cb);
}

YtmdSnapshot YtmdClient::snapshot() const
{
	QMutexLocker lock(&m_mutex);
	return m_snapshot;
}

void YtmdClient::onStateUpdated(const YtmdTrackInfo &track, qint64 pollTimeMs)
{
	QMutexLocker lock(&m_mutex);
	m_snapshot.track = track;
	m_snapshot.lastPollMs = pollTimeMs;
	m_snapshot.version++;
}

void YtmdClient::onAlbumArtUpdated(const QString &url, const QImage &image)
{
	QMutexLocker lock(&m_mutex);
	if (m_snapshot.track.thumbnailUrl == url || m_snapshot.track.thumbnailUrl.isEmpty()) {
		m_snapshot.albumArt = image;
		m_snapshot.version++;
	}
}

void YtmdClient::onStatusChanged(const QString &status)
{
	QMutexLocker lock(&m_mutex);
	m_snapshot.statusText = status;
	m_snapshot.version++;
}

void YtmdClient::onTokenReceived(const QString &token)
{
	std::function<void(const QString &)> cb;
	{
		QMutexLocker lock(&m_mutex);
		m_snapshot.token = token;
		m_snapshot.version++;
		cb = m_tokenCallback;
	}
	if (cb) {
		cb(token);
	}
}
