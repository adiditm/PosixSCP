//---------------------------------------------------------------------------
// operationdialog.h — WinSCP-style connecting & transfer progress dialogs.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_OPERATIONDIALOG_H
#define WINSCP_UIQT_OPERATIONDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QPixmap>
#include <atomic>
#include <memory>
#include "enginebridge.h"

class QListWidget;

//===========================================================================
// ConnectingDialog — WinSCP connecting dialog (matches WinSCP session connect).
// Shows dual-arrow lock icon on left and clean sequential status steps on right.
//===========================================================================
class ConnectingDialog : public QDialog
{
  Q_OBJECT
public:
  ConnectingDialog(QWidget * parent, const QString & sessionTitle, const engine::ConnectOptions & o);

  void addStep(const QString & text);
  void setStatus(const QString & text);

private:
  QLabel * m_icon;
  QListWidget * m_list;
};

//===========================================================================
// TransferProgressDialog — WinSCP-style Copy/Upload/Download ProgressForm.
// Shows current file, target directory, file & overall progress bars,
// transfer speed, elapsed/left time, [Background], and [Cancel].
//===========================================================================
class TransferProgressDialog : public QDialog
{
  Q_OBJECT
public:
  TransferProgressDialog(QWidget * parent, const QString & opName,
                         int totalFiles, std::shared_ptr<std::atomic<bool>> cancelFlag);

  void setCurrentFile(const QString & fileName, const QString & targetDir, int fileIndex);
  void updateProgress(const engine::TransferProgress & tp, int fileIndex);
  void finishTransfer();

signals:
  void backgroundRequested();

private:
  QString m_opName;
  int m_totalFiles;
  std::shared_ptr<std::atomic<bool>> m_cancelFlag;
  QElapsedTimer m_timer;

  QLabel * m_icon;
  QLabel * m_lblTitle;
  QLabel * m_lblFile;
  QLabel * m_lblTarget;
  QProgressBar * m_pbFile;
  QProgressBar * m_pbTotal;
  QLabel * m_lblTotalCount;

  QLabel * m_lblTransferred;
  QLabel * m_lblSpeed;
  QLabel * m_lblElapsed;
  QLabel * m_lblTimeLeft;

  QPushButton * m_btnBackground;
  QPushButton * m_btnCancel;
};

#endif
