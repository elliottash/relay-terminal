#include <QApplication>
#include <QFontDatabase>
#include <QElapsedTimer>
#include <QWidget>
#include <QIcon>
#include <QDateTime>
#include <cstdio>
#include <time.h>
static double now_ms(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
int main(int argc,char**argv){
  double t0=now_ms();
  QApplication app(argc,argv);
  double tApp=now_ms();
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  double tSys=now_ms();
  QStringList fams = QFontDatabase().families();
  double tFams=now_ms();
  QIcon ic = QIcon::fromTheme("org.relayterminal.Relay");
  double tIcon=now_ms();
  QWidget w; w.resize(1200,800); w.show(); app.processEvents();
  double tShow=now_ms();
  printf("QApplication_ctor=%.1f systemFont=%.1f families=%.1f(n=%d) iconFromTheme=%.1f widgetShow=%.1f total=%.1f\n",
    tApp-t0,tSys-tApp,tFams-tSys,fams.size(),tIcon-tFams,tShow-tIcon,tShow-t0);
  return 0;
}
