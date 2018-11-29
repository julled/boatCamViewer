#include "boxTracker.h"
#include "helpingfunctions.h"
#include <QElapsedTimer>
#include <caffe/caffe.hpp>

template<typename QEnum>
std::string QtEnumToString (const QEnum value)
{
  return std::string(QMetaEnum::fromType<QEnum>().valueToKey(value));
}

boxTracker::boxTracker(boxTracker::trackertype tracker)
{
    m_curTrackertype=tracker;
    resetTracker();
}

boxTracker::~boxTracker()
{
    m_isInitalized=false;
    quit();
    wait();
    trackerMutex.lock();
    m_openCvTracker.release();
}
void boxTracker::createNewTracker(boxTracker::trackertype tracker)
{
    resetTracker();
    m_curTrackertype=tracker;
    cv::String m_trackerString=QtEnumToString(m_curTrackertype);

    emit sendControlMsg(ImageIO::ctrlMsg::pause);
    if(m_curTrackertype!=trackertype::caffeGOTURN){
        m_openCvTracker = createOpenCvTrackerByName(m_trackerString);
    }
    else if(m_curTrackertype==trackertype::caffeGOTURN){
        m_caffeGoturnTracker=createCaffeGoturnTracker();
    }
    emit sendControlMsg(ImageIO::ctrlMsg::play);
}
void boxTracker::resetTracker()
{
    m_isInitalized=false;
    m_currImage = NULL;

    quit();
    wait();
    trackerMutex.lock();
    m_openCvTracker.release();
    //m_curTrackertype=tracker;

    m_initRoiReceived = false;
    m_frameCounter=0;
    m_curFps=0;
    m_currImageProcessed=false;
    m_curTrackedRoi = QRect(0,0,0,0);
    m_curTrackerStatus=trackerstatus::NO_TRACK;

    trackerMutex.unlock();
    emit sendTrackerInfo(createTrackerInfo());

}

bool boxTracker::init(cv::Mat image, cv::Rect2d boundingbox)
{

    if(!m_isInitalized){
        //m_isInitalized=false;
        createNewTracker(m_curTrackertype);
    }
    bool ok=false;
    if(m_curTrackertype!=trackertype::caffeGOTURN){
        if (m_openCvTracker != NULL){
            trackerMutex.lock();
            ok = m_openCvTracker->init(image,boundingbox);
            m_currImageProcessed = true;
            trackerMutex.unlock();

        }
    }
    else if(m_curTrackertype==trackertype::caffeGOTURN){
        trackerMutex.lock();
        std::vector<float> bbData = {boundingbox.x,boundingbox.y,boundingbox.x+boundingbox.width,boundingbox.y+boundingbox.height};
        BoundingBox goturnBB(bbData);
        m_caffeGoturnTracker.Init(image, goturnBB, &m_caffeGoturnRegressor);
        trackerMutex.unlock();
        ok= true;
    }
    if(ok){
        m_isInitalized=true;
        start(QThread::LowPriority);
    }

}

qint8 boxTracker::update(cv::Mat image, cv::Rect2d &boundingbox)
{

    trackerMutex.lock();
    int ok;
    if(m_curTrackertype!=trackertype::caffeGOTURN){
        try {
          //image = cv::Mat(100,100, CV_64F, cvScalar(0.));
          ok = m_openCvTracker->update(image,boundingbox);
        }
        catch(cv::Exception e){
            ok = true;
        }
    }
    else if(m_curTrackertype==trackertype::caffeGOTURN){
        BoundingBox bbox_estimate;
        //try {
            m_caffeGoturnTracker.Track(image, &m_caffeGoturnRegressor, &bbox_estimate);
        //}
        //catch(cv::Exception e){
        //}
        boundingbox.x = bbox_estimate.x1_;
        boundingbox.y = bbox_estimate.y1_;
        boundingbox.width = bbox_estimate.x2_-bbox_estimate.x1_;
        boundingbox.height = bbox_estimate.y2_-bbox_estimate.y1_;
    }

    trackerMutex.unlock();
    qint8 qok = static_cast<qint8>(ok);
    return qok;
}

QList<QString> boxTracker::getTrackerTypes()
{
    QList<QString> trackerNameList;
    for ( uint i = 0; i < 9; i++ )
    {
        QString trackerName = QString::fromStdString(QtEnumToString(static_cast<trackertype>(i)));
        trackerNameList.append(trackerName);
    }
    return trackerNameList;
}

void boxTracker::run()
{
     msleep(500);
    /* In Thread caffe needs to be set up again
     * https://github.com/BVLC/caffe/issues/4178#issuecomment-221386875*/
    if(m_curTrackertype==trackertype::caffeGOTURN){
        caffe::Caffe::set_mode(caffe::Caffe::GPU);
    }

    QElapsedTimer timeMeasurement;
    timeMeasurement.start();
    while(m_isInitalized){
        if(!m_currImageProcessed)
        {
            cv::Rect2d trackedRoi;
            if(m_currImage.empty())
                continue;
            //cv::Mat image(100,100, CV_64F, cvScalar(0.));
            bool ok = update(m_currImage,trackedRoi);

            if( !ok ||
                trackedRoi.x < 0 || trackedRoi.x > m_currImage.cols || trackedRoi.width > m_currImage.cols ||
                trackedRoi.y < 0 || trackedRoi.y > m_currImage.rows || trackedRoi.height > m_currImage.rows )
//                  || trackedRoi.size().area()==0)
            {
                m_curFps = 0;
                m_curTrackerStatus=trackerstatus::NO_TRACK;
                emit sendTrackerInfo(createTrackerInfo());
                resetTracker();
                break;
            }
            if(ok){
                QRect qtrackedRoi = QRect(trackedRoi.x,trackedRoi.y,trackedRoi.width,trackedRoi.height);
                m_curTrackedRoi = qtrackedRoi;
                m_curTrackerStatus=trackerstatus::TRACKING;

                m_frameCounter++;
                float elapsedTimeSec = timeMeasurement.elapsed() / 1000.0;
                timeMeasurement.restart();
                if(elapsedTimeSec > 1.0){
                    m_curFps = m_frameCounter/elapsedTimeSec;
                    m_frameCounter=0;
                }
                emit sendTrackerInfo(createTrackerInfo());
                m_currImageProcessed=true;
            }
        }

        msleep(5);
    }
    return;
}

void boxTracker::receiveRoi(const QRect &roi)
{
    cv::Point p1(roi.topLeft().x(),roi.topLeft().y());
    cv::Point p2(roi.bottomRight().x(),roi.bottomRight().y());
    m_initRoi=cv::Rect(p1,p2);
    resetTracker();
    if(init(m_currImage,m_initRoi)){
        m_isInitalized=true;
        m_initRoiReceived = false;
    }
    //if(!m_isInitalized){
    //    if(init(m_currImage,m_initRoi)){
    //        m_isInitalized=true;
    //        m_initRoiReceived = false;
    //    }
    //}
    //else{
    //    m_initRoiReceived = true;
    //    //m_isInitalized = false;
    //}
}

void boxTracker::receiveImage(cv::Mat newImage)
{
    //if(!m_isInitalized && m_initRoiReceived){
    //    if( init(newImage,m_initRoi)){
    //        m_isInitalized=true;
    //        m_initRoiReceived = false;
    //    }
    //}
    //else if(m_isInitalized){
    //    m_currImage = newImage;
    //}
    m_currImage = newImage;
    m_currImageProcessed=false;
}

boxTracker::trackerInfo boxTracker::createTrackerInfo(){
    boxTracker::trackerInfo info;
    info.roi = m_curTrackedRoi;
    info.roiCenter = QPoint(m_curTrackedRoi.topLeft().x() + m_curTrackedRoi.width()/2,m_curTrackedRoi.topLeft().y() + m_curTrackedRoi.height()/2);
    cv::Size imgSize = m_currImage.size();
    info.deltasCenter = QPoint(info.roiCenter.x() - imgSize.width/2,info.roiCenter.y() - imgSize.height/2);
    info.status = m_curTrackerStatus;
    info.fps = static_cast<int>(m_curFps);
    return info;
}

cv::Ptr<cv::Tracker> boxTracker::createOpenCvTrackerByName(cv::String name)
{
    cv::Ptr<cv::Tracker> tracker;

    if (name == "KCF")
        tracker = cv::TrackerKCF::create();
    else if (name == "TLD")
        tracker = cv::TrackerTLD::create();
    else if (name == "BOOSTING")
        tracker = cv::TrackerBoosting::create();
    else if (name == "MEDIANFLOW")
        tracker = cv::TrackerMedianFlow::create();
    else if (name == "MIL")
        tracker = cv::TrackerMIL::create();
    else if (name == "GOTURN")
        tracker = cv::TrackerGOTURN::create();
    else if (name == "MOSSE")
        tracker = cv::TrackerMOSSE::create();
    else if (name == "CSRT")
        tracker = cv::TrackerCSRT::create();
    else
        CV_Error(cv::Error::StsBadArg, "Invalid tracking algorithm name\n");

    return tracker;
}

Tracker boxTracker::createCaffeGoturnTracker()
{
    //::google::InitGoogleLogging("");//argv[0]);

    const std::string& model_file   = QString("/home/julle/trackerProg/GOTURN/nets/tracker.prototxt").toStdString();//argv[1];
    const std::string& trained_file = QString("/home/julle/trackerProg/GOTURN/nets/models/pretrained_model/tracker.caffemodel").toStdString();//argv[2];

    int gpu_id = 0;
    //if (argc >= 4) {
    //  gpu_id = atoi(argv[3]);
    //}

    const bool do_train = false;
    m_caffeGoturnRegressor=Regressor(model_file, trained_file, gpu_id, do_train);

    // Ensuring randomness for fairness.
    srandom(time(NULL));

    // Create a tracker object.
    const bool show_intermediate_output = false;
    return Tracker(show_intermediate_output);

    //VOT vot; // Initialize the communcation

    // Get region and first frame
    //VOTRegion region = vot.region();
    //string path = vot.frame();


}

