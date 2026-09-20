// What one Pane::pollGuestEvents() tick costs, and what a stat-gated version would cost.
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QElapsedTimer>
#include <sys/stat.h>
#include <cstdio>
int main(int argc,char**argv){
  QCoreApplication app(argc,argv);
  QCoreApplication::setOrganizationName("RelayTerminal");
  QCoreApplication::setApplicationName("relay");
  const QString dir = argv[1];
  QDir().mkpath(dir);
  const int N=20000;
  QElapsedTimer t;
  t.start();
  for(int i=0;i<N;i++){ QDir d(dir); if(!d.exists()) continue; volatile int n=d.entryList(QStringList{"*.json"},QDir::Files,QDir::Name).size(); (void)n; }
  double entry=t.nsecsElapsed()/double(N)/1000.0;
  t.restart();
  for(int i=0;i<N;i++){ struct stat s; ::stat(qPrintable(dir),&s); volatile long m=s.st_mtim.tv_nsec; (void)m; }
  double st=t.nsecsElapsed()/double(N)/1000.0;
  t.restart();
  for(int i=0;i<N;i++){ volatile bool b=QSettings().value("appearance/pane_usage",true).toBool(); (void)b; }
  double qs=t.nsecsElapsed()/double(N)/1000.0;
  printf("QDir::exists+entryList(empty dir) = %.2f us/call\nstat(dir) = %.2f us/call\nQSettings().value().toBool() = %.2f us/call\n",entry,st,qs);
  return 0;
}
