#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <cstdint>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct YtmdTrackInfo {
	QString videoId;
	QString title;
	QString author;
	QString album;
	QString thumbnailUrl;
	double durationSeconds = 0.0;
	double videoProgress = 0.0;
	int trackState = -1; // -1: unknown, 0: paused, 1: playing, 2: buffering
	bool valid = false;
};

struct YtmdSnapshot {
	YtmdTrackInfo track;
	QImage albumArt;
	QString statusText;
	QString token;
	uint64_t version = 0;
	qint64 lastPollMs = 0;
};

class YtmdWorker : public QObject {
	Q_OBJECT
public:
	explicit YtmdWorker(QObject *parent = nullptr);
	~YtmdWorker() override;

public slots:
	void start();
	void stop();
	void setConfig(const QString &host, int port, const QString &token);
	void startPairing();

signals:
	void stateUpdated(const YtmdTrackInfo &track, qint64 pollTimeMs);
	void albumArtUpdated(const QString &url, const QImage &image);
	void statusChanged(const QString &status);
	void tokenReceived(const QString &token);

private slots:
	void pollState();

private:
	QString baseUrl() const;
	void fetchThumbnail(const QString &url);

	QNetworkAccessManager *m_nam = nullptr;
	QTimer *m_timer = nullptr;
	QString m_host = QStringLiteral("127.0.0.1");
	int m_port = 9863;
	QString m_token;
	QString m_lastThumbnailUrl;
	bool m_pollInFlight = false;
	bool m_pairingInFlight = false;
};

class YtmdClient : public QObject {
	Q_OBJECT
public:
	explicit YtmdClient(QObject *parent = nullptr);
	~YtmdClient() override;

	void setConfig(const QString &host, int port, const QString &token);
	void requestPairing();
	void setTokenCallback(std::function<void(const QString &)> cb);

	YtmdSnapshot snapshot() const;

private slots:
	void onStateUpdated(const YtmdTrackInfo &track, qint64 pollTimeMs);
	void onAlbumArtUpdated(const QString &url, const QImage &image);
	void onStatusChanged(const QString &status);
	void onTokenReceived(const QString &token);

private:
	QThread m_thread;
	YtmdWorker *m_worker = nullptr;

	mutable QMutex m_mutex;
	YtmdSnapshot m_snapshot;
	std::function<void(const QString &)> m_tokenCallback;
};
