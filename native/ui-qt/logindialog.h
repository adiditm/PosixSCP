//---------------------------------------------------------------------------
// logindialog.h — the Login dialog (sites + Session form + Advanced: proxy / SSH tunnel / FTP),
// saved sites, and the authentication prompt (password, passphrase, keyboard-interactive / 2FA).
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_LOGINDIALOG_H
#define WINSCP_UIQT_LOGINDIALOG_H

#include <QString>
#include <QStringList>
#include <vector>
#include <string>
#include "enginebridge.h"

class QWidget;

struct LoginParams
{
  bool ok = false;
  QString siteName;
  engine::ConnectOptions o;
  QStringList workspaceSites; // non-empty if user opened a workspace
};

// Shows the Login dialog. `preset` (optional) pre-fills the form.
LoginParams showLoginDialog(QWidget * parent, const engine::ConnectOptions * preset = nullptr);
bool showAdvancedDialog(QWidget * parent, engine::ConnectOptions & o);

// Saved sites (QSettings group "sites/<name>").
QStringList savedSiteNames();
bool loadSite(const QString & name, engine::ConnectOptions & o);
void saveSite(const QString & name, const engine::ConnectOptions & o);
void deleteSite(const QString & name);
int importWinScpIniFile(const QString & filePath, QString * outError = nullptr);
QString decryptWinScpPassword(const QString & encryptedHex, const QString & key);

// "SFTP", "SCP", "FTP", "WebDAV", "S3".
QString protocolName(engine::Protocol p);
// One line describing the non-default connection route ("SSH tunnel via u@h · SOCKS5 proxy h:1080");
// empty when the connection is direct.
QString routeSummary(const engine::ConnectOptions & o);
// Resolves WinSCP Windows-style or Wine path (e.g. Z:\ProjectsY\...) to the native macOS filesystem path.
QString resolveLocalPath(const QString & in);

// Asks the user the questions of an authentication prompt. Returns false if cancelled.
// Headless runs take the answers from WINSCP_PROMPT_ANSWERS ("a|b|c", consumed in order) and cancel
// when it runs out.
bool showAuthPrompt(QWidget * parent, const engine::PromptRequest & rq, std::vector<std::string> & answers);

#endif
