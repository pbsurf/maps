#include "windowsPlatform.h"
#include "gl/hardware.h"
#include "log.h"
#include <stdio.h>

//#include <GLFW/glfw3.h>

#define DEFAULT "res/fonts/NotoSans-Regular.ttf"
#define FONT_AR "res/fonts/NotoNaskh-Regular.ttf"
#define FONT_HE "res/fonts/NotoSansHebrew-Regular.ttf"
#define FONT_JA "res/fonts/DroidSansJapanese.ttf"
#define FALLBACK "res/fonts/DroidSansFallback.ttf"

namespace Tangram {

bool WindowsPlatform::logToConsole = false;

void logStr(const std::string& msg) {
    if (WindowsPlatform::logToConsole) {
        fprintf(stderr, msg.c_str());
    } else {
        OutputDebugStringA(msg.c_str());
    }
}

WindowsPlatform::WindowsPlatform()
    : WindowsPlatform(UrlClient::Options{}) {}

WindowsPlatform::WindowsPlatform(UrlClient::Options urlClientOptions) :
    m_urlClient(std::make_unique<UrlClient>(urlClientOptions)) {
}

WindowsPlatform::~WindowsPlatform() {}

void WindowsPlatform::shutdown() {
    // Stop all UrlWorker threads
    m_urlClient.reset();

    Platform::shutdown();
}

//~ void WindowsPlatform::requestRender() const {
//~     if (m_shutdown) { return; }
//~     glfwPostEmptyEvent();
//~ }

std::vector<FontSourceHandle> WindowsPlatform::systemFontFallbacksHandle() const {
    std::vector<FontSourceHandle> handles;

    //handles.emplace_back(Url(DEFAULT));
    //handles.emplace_back(Url(FONT_AR));
    //handles.emplace_back(Url(FONT_HE));
    //handles.emplace_back(Url(FONT_JA));
    //handles.emplace_back(Url(FALLBACK));

    return handles;
}

bool WindowsPlatform::startUrlRequestImpl(const Url& _url, const HttpOptions& _options, const UrlRequestHandle _request, UrlRequestId& _id) {
    _id = m_urlClient->addRequest(_url.string(), _options,
                                  [this, _request](UrlResponse&& response) {
                                      onUrlResponse(_request, std::move(response));
                                  });
    return true;
}

void WindowsPlatform::cancelUrlRequestImpl(const UrlRequestId _id) {
    if (!m_urlClient) { return; }
    if (_id == UrlRequestId(-1)) {
        m_urlClient->cancelAllRequests();
    } else {
        m_urlClient->cancelRequest(_id);
    }
}

void setCurrentThreadPriority(int priority) {}

void initGLExtensions() {
    Tangram::Hardware::supportsMapBuffer = true;
}

} // namespace Tangram
