#include <QtCore>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <QUuid>
#include <QHttp>
#include <QHttpHeader>
#include <QUrl>

#include <QByteArray>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <qjson/parser.h> 

#include "danmaku.h"


Subscriber::Subscriber(QString server, QString channel, QString passwd, QObject* parent)
	: QThread(parent)
{
	this->server = server;
	this->channel = channel;
	this->passwd = passwd;
	
	QUuid uuid = QUuid::createUuid();
	this->_uuid = uuid.toString();
	// myDebug << this->_uuid;
	
	QString uri = QString("/api/v1.1/channels/%1/danmaku").arg(this->channel);
	QUrl baseUrl = QUrl(this->server);
	// myDebug << baseUrl.host() << baseUrl.port();

	qint16 port = baseUrl.port(80);
	QHttp::ConnectionMode mode = QHttp::ConnectionModeHttp;

	if(baseUrl.scheme().compare("https") == 0) {
		port = baseUrl.port(443);
		mode = QHttp::ConnectionModeHttps;
	}
	
	
	http = new QHttp(baseUrl.host(), mode, port, this);
	header = QHttpRequestHeader("GET", uri);
	header.setValue("Host", baseUrl.host());
	header.setValue("X-GDANMAKU-SUBSCRIBER-ID", this->_uuid);
	header.setValue("X-GDANMAKU-AUTH-KEY", this->passwd);
	
	// connect(http, SIGNAL(done(bool)), this, SLOT(parse_response(bool)));
	connect(this, SIGNAL(terminated()), this, SLOT(deleteLater()));
	connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void Subscriber::run() 
{
	mark_stop = false;
	
	QEventLoop loop;
	connect(http, SIGNAL(done(bool)), &loop, SLOT(quit()));
	connect((DMApp *)this->parent(), SIGNAL(stop_subscription()),
			&loop, SLOT(quit()));
	
	// Set HTTP request timeout
	QTimer timeout;
	timeout.setSingleShot(true);
	// If timeout signaled, let http request abort
	connect(&timeout, SIGNAL(timeout()), http, SLOT(abort()));
	
	while(1) {
		timeout.start(10000);
		http->request(header);
		loop.exec();
		timeout.stop();
		if(mark_stop) {
			myDebug << "Thread marked to stop";
			break;
		}
		if(http->error()){
			myDebug << http->errorString() << "Wait 2 secs";
			this->msleep(2000);
		} else {
			parse_response();
		}
	}
}


void Subscriber::parse_response() {
	QHttpResponseHeader resp = http->lastResponse();
	if(resp.isValid()) {
		bool fatal = false;
		int statusCode = resp.statusCode();
		if (statusCode >= 400) {
			fatal = true;
			QString errMsg;
			if (statusCode == 403 ) {
				errMsg = "Wrong Password";
			} else if (statusCode == 404) {
				errMsg = "No Such Channel";
			} else if (statusCode >= 500) {
				errMsg = "Server Error";
			}
			myDebug << errMsg;
			emit new_alert(errMsg);
		}
		if (fatal) {
			return;
		}
	}
	
	bool ok;
	QJson::Parser  parser;
	
	// QByteArray json = QByteArray(
	// 		"[{\"text\": \"test\", \"style\": \"white\", \"position\": \"fly\"},"
	// 		"{\"text\": \"test2\", \"style\": \"white\", \"position\": \"fly\"}]"
	// );
	QByteArray json = http->readAll();

	QVariant res = parser.parse(json, &ok);

	if(ok) {
		QVariantList dms = res.toList();
		for(QVariantList::iterator i = dms.begin(); i != dms.end(); ++i) {
			QVariantMap dm = i->toMap();
			QString text = dm["text"].toString(),
					color = dm["style"].toString(),
					position = dm["position"].toString();
			myDebug << text ;

			emit new_danmaku(text, color, position);
		}
	}
}

