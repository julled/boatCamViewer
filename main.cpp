#include "mainwindow.h"
#include <QApplication>
#include <opencv2/opencv.hpp>
#include "boxTracker.h"

Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(boxTracker::trackerInfo)
Q_DECLARE_METATYPE(ImageIO::ctrlMsg)

int main(int argc, char *argv[])
{
    qRegisterMetaType<cv::Mat>();
    qRegisterMetaType<boxTracker::trackerInfo>();
    qRegisterMetaType<ImageIO::ctrlMsg>();

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    //std::cout << cv::getBuildInformation() << std::endl;

    return a.exec();
}
