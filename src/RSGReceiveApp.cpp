#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/qtime/AvfWriter.h"

#include "cinder/Log.h"
#include "cinder/Timeline.h"
#include "cinder/osc/Osc.h"
#include "cinder/params/Params.h"

using namespace ci;
using namespace ci::app;
using namespace std;

#define USE_UDP 1

#if USE_UDP
using Receiver = osc::ReceiverUdp;
using protocol = asio::ip::udp;
#else
using Receiver = osc::ReceiverTcp;
using protocol = asio::ip::tcp;
#endif

const uint16_t localPort = 10001;

class RSGReceiveApp : public App {
  public:
    RSGReceiveApp();
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
    void render();
    void button();
    
    Receiver mReceiver;
    std::map<uint64_t, protocol::endpoint> mConnections;
    
    gl::TextureRef	mFloorPlan;
    
    // for the text box
    gl::TextureRef	mTextTexture;
    vec2	mSize;
    Font	mFont;
    
    int mTrailLimit;
    int mPageNum;
    bool mRecord;
    
    vector<vec2> mCurrentCirclePos1s;
    vec2 mCurrentCirclePos2;
    vec2 mCurrentCirclePos3;
    vector<vec2> mTrails;
    
    // quicktime
    qtime::MovieWriterRef mMovieExporter;
    qtime::MovieWriter::Format format;

private:
    params::InterfaceGlRef mParams;
};

RSGReceiveApp::RSGReceiveApp()
: mReceiver( localPort )
{
}

void RSGReceiveApp::setup()
{
    mPageNum = 1;
    
    setFullScreen(true);
    
    // load floor plan image
    try {
        mFloorPlan = gl::Texture::create( loadImage( loadAsset("TOMS_GP_v7.6.png") ) );
    }
    catch( ... ) {
        std::cout << "unable to load the texture file!" << std::endl;
    }
    
    // quicktime
    fs::path path = getSaveFilePath();
    if( !path.empty() ) {
        format = qtime::MovieWriter::Format().codec(qtime::MovieWriter::H264).fileType(qtime::MovieWriter::QUICK_TIME_MOVIE).setTimeScale(250);
        
        mMovieExporter = qtime::MovieWriter::create(path, getWindowWidth(), getWindowHeight(), format );
    }
    
    mCurrentCirclePos1s.push_back(*new vec2(0,0));
    mCurrentCirclePos1s.push_back(*new vec2(0,0));
    mCurrentCirclePos1s.push_back(*new vec2(0,0));

    
    // set up OSC
    mReceiver.setListener( "/pos/front/0",
                          [&]( const osc::Message &msg ){
                              mCurrentCirclePos1s[0].x = msg[0].flt() * getWindowWidth();
                              mCurrentCirclePos1s[0].y = msg[1].flt() * getWindowHeight();
                          });
    mReceiver.setListener( "/pos/front/1",
                          [&]( const osc::Message &msg ){
                              mCurrentCirclePos1s[1].x = msg[0].flt() * getWindowWidth();
                              mCurrentCirclePos1s[1].y = msg[1].flt() * getWindowHeight();
                          });
    mReceiver.setListener( "/pos/front/2",
                          [&]( const osc::Message &msg ){
                              mCurrentCirclePos1s[2].x = msg[0].flt() * getWindowWidth();
                              mCurrentCirclePos1s[2].y = msg[1].flt() * getWindowHeight();
                          });
    mReceiver.setListener( "/pos/2",
                          [&]( const osc::Message &msg ){
                              mCurrentCirclePos2.x = msg[0].flt() * getWindowWidth();
                              mCurrentCirclePos2.y = msg[1].flt() * getWindowHeight();
                          });
    mReceiver.setListener( "/pos/3",
                          [&]( const osc::Message &msg ){
                              mCurrentCirclePos3.x = msg[0].flt() * getWindowWidth();
                              mCurrentCirclePos3.y = msg[1].flt() * getWindowHeight();
                          });
    try {
        // Bind the receiver to the endpoint. This function may throw.
        mReceiver.bind();
    }
    catch( const osc::Exception &ex ) {
        CI_LOG_E( "Error binding: " << ex.what() << " val: " << ex.value() );
        quit();
    }
#if USE_UDP
    // UDP opens the socket and "listens" accepting any message from any endpoint. The listen
    // function takes an error handler for the underlying socket. Any errors that would
    // call this function are because of problems with the socket or with the remote message.
    mReceiver.listen(
                     []( asio::error_code error, protocol::endpoint endpoint ) -> bool {
                         if( error ) {
                             CI_LOG_E( "Error Listening: " << error.message() << " val: " << error.value() << " endpoint: " << endpoint );
                             return false;
                         }
                         else
                             return true;
                     });
#else
    mReceiver.setConnectionErrorFn(
                                   // Error Function for Accepted Socket Errors. Will be called anytime there's an
                                   // error reading from a connected socket (a socket that has been accepted below).
                                   [&]( asio::error_code error, uint64_t identifier ) {
                                       if ( error ) {
                                           auto foundIt = mConnections.find( identifier );
                                           if( foundIt != mConnections.end() ) {
                                               // EOF or end of file error isn't specifically an error. It's just that the
                                               // other side closed the connection while you were expecting to still read.
                                               if( error == asio::error::eof ) {
                                                   CI_LOG_W( "Other side closed the connection: " << error.message() << " val: " << error.value() << " endpoint: " << foundIt->second.address().to_string()
                                                            << " port: " << foundIt->second.port() );
                                               }
                                               else {
                                                   CI_LOG_E( "Error Reading from Socket: " << error.message() << " val: "
                                                            << error.value() << " endpoint: " << foundIt->second.address().to_string()
                                                            << " port: " << foundIt->second.port() );
                                               }
                                               mConnections.erase( foundIt );
                                           }
                                       }
                                   });
    auto expectedOriginator = protocol::endpoint( asio::ip::address::from_string( "127.0.0.1" ), 10000 );
    mReceiver.accept(
                     // Error Handler for the acceptor. You'll return true if you want to continue accepting
                     // or fals otherwise.
                     []( asio::error_code error, protocol::endpoint endpoint ) -> bool {
                         if( error ) {
                             CI_LOG_E( "Error Accepting: " << error.message() << " val: " << error.value()
                                      << " endpoint: " << endpoint.address().to_string() );
                             return false;
                         }
                         else
                             return true;
                     },
                     // Accept Handler. Return whether or not the acceptor should cache this connection
                     // (true) or dismiss it (false).
                     [&, expectedOriginator]( osc::TcpSocketRef socket, uint64_t identifier ) -> bool {
                         // Here we return whether or not the remote endpoint is the expected endpoint
                         mConnections.emplace( identifier, socket->remote_endpoint() );
                         return socket->remote_endpoint() == expectedOriginator;
                     } );
#endif

    
    // setting up the text box
#if defined( CINDER_COCOA )
    mFont = Font( "Helvetica", 24 );
#else
    mFont = Font( "Times New Roman", 32 );
#endif
    mSize = vec2( 100, 100 );
    
    render();
    
    
    
    mTrailLimit = 100;
    mPageNum = 0;
    mRecord = false;
    
    
    
    // set up parameters
    // Create the interface and give it a name.
    mParams = params::InterfaceGl::create( getWindow(), "Ready Set Go", toPixels( ivec2( 200, 200 ) ) );
    //    mParams->addParam("Fade Out", &mTrailLimit).min(0.0).max(300).step(2);
    mParams->addParam("Fade Out", &mTrailLimit);
    mParams->addParam("Recording", &mRecord);
    mParams->addButton( "Next Topic", bind( &RSGReceiveApp::button, this ) );
    //mParams->addButton( "Next Page", bind( {mPageNum++;} ) );
    
    
    
    // quicktime setup
//#if defined( CINDER_COCOA_TOUCH )
//    
//    auto format = qtime::MovieWriter::Format().codec( qtime::MovieWriter::JPEG ).fileType( qtime::MovieWriter::QUICK_TIME_MOVIE ).
//    
//    jpegQuality( 0.09f ).averageBitsPerSecond( 10000000 );
//    
//    mMovieExporter = qtime::MovieWriter::create( getDocumentsDirectory() / "test.mov", getWindowWidth(), getWindowHeight(), format );
//    
//#else
//    
//    fs::path path = getSaveFilePath();
//    
//    if( ! path.empty() ) {
//        
//        auto format = qtime::MovieWriter::Format().codec( qtime::MovieWriter::H264 ).fileType( qtime::MovieWriter::QUICK_TIME_MOVIE )
//        .jpegQuality( 0.09f ).averageBitsPerSecond( 10000000 );
//        mMovieExporter = qtime::MovieWriter::create( path, getWindowWidth(), getWindowHeight(), format );
//    }
//    
//#endif
//    
//    gl::bindStockShader( gl::ShaderDef().color() );
}

void RSGReceiveApp::render()
{
    string txt = "Topic " + std::to_string(mPageNum);
    TextBox tbox = TextBox().alignment( TextBox::LEFT ).font( mFont ).size( ivec2( mSize.x, TextBox::GROW ) ).text( txt );
    tbox.setColor( Color::black() );
    //    tbox.setBackgroundColor( ColorA( 0.5, 0, 0, 1 ) );
    // ivec2 sz = tbox.measure();
    // console() << "Height: " << sz.y << endl;
    mTextTexture = gl::Texture2d::create( tbox.render() );
}

void RSGReceiveApp::button()
{
    mPageNum++;
    render();
}

void RSGReceiveApp::mouseDown( MouseEvent event )
{
    mTrails.clear();
}

void RSGReceiveApp::update()
{
    if(mMovieExporter && getElapsedFrames() > 1 && getElapsedFrames() < 100000)
        mMovieExporter->addFrame(copyWindowSurface());
    else if( mMovieExporter && getElapsedFrames() >= 100000 ) {
        mMovieExporter->finish();
        mMovieExporter.reset();
    }
}

void RSGReceiveApp::draw()
{
    gl::clear( Color( 0, 0, 0 ) );
    gl::color(Color::white());
    gl::draw(mFloorPlan, Rectf(0,0,getWindowWidth(), getWindowHeight()));
    
    // draw page number
    if( mTextTexture )
        gl::draw( mTextTexture, Rectf(getWindowWidth()-130,0,getWindowWidth(),60));
    
    gl::color(Color(1,0,0));
    for(vec2 c: mCurrentCirclePos1s) {
        gl::drawSolidCircle(c, 25);
        mTrails.push_back(c);
    }
    gl::drawSolidCircle(mCurrentCirclePos2, 25);
    mTrails.push_back(mCurrentCirclePos2);
    gl::drawSolidCircle(mCurrentCirclePos3, 25);
    mTrails.push_back(mCurrentCirclePos3);
    
    for(vec2 t: mTrails) {
        gl::drawSolidCircle(t, 5);
    }
    
    mParams->draw();

//    // Syphon
//    if ( mRecord ) {
//        
//        mScreenSyphon.publishScreen(); //publish the screen's output
//        
//    }
    
    
    
    // anything after this line will not be published
}

auto settingsFunc = []( App::Settings *settings ) {
#if defined( CINDER_MSW )
    settings->setConsoleWindowEnabled();
#endif
    settings->setMultiTouchEnabled( false );
};

CINDER_APP( RSGReceiveApp, RendererGl, settingsFunc )
