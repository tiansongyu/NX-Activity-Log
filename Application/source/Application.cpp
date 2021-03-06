#include "Application.hpp"
#include "utils/Curl.hpp"
#include "utils/Lang.hpp"
#include "utils/UpdateUtils.hpp"
#include "utils/Utils.hpp"

#include "ui/screen/AllActivity.hpp"
#include "ui/screen/CustomTheme.hpp"
#include "ui/screen/Details.hpp"
#include "ui/screen/RecentActivity.hpp"
#include "ui/screen/Settings.hpp"
#include "ui/screen/Update.hpp"
#include "ui/screen/UserSelect.hpp"

// Path to background image
#define BACKGROUND_IMAGE "/config/NX-Activity-Log/background.png"

namespace Main {
    Application::Application() {
        // Start all required services
        Utils::NX::startServices();
        Utils::Curl::init();

        // Create config object and read in values
        this->config_ = new Config();
        this->config_->readConfig();

        // Set language
        if (!Utils::Lang::setLanguage(this->config_->gLang())) {
            this->display->exit();
        }

        this->playdata_ = new NX::PlayData();
        this->theme_ = new Theme(this->config_->gTheme());

        // Start update thread
        this->hasUpdate_ = false;
        this->updateThread = std::async(std::launch::async, &Application::checkForUpdate, this);

        // Check if launched via user page and if so only use the chosen user
        NX::User * u = Utils::NX::getUserPageUser();
        this->isUserPage_ = false;
        if (u != nullptr) {
            this->isUserPage_ = true;
            this->users.push_back(u);
        }

        // Set view to today and by day
        this->tm = Utils::Time::getTmForCurrentTime();
        this->tm.tm_hour = 0;
        this->tm.tm_min = 0;
        this->tm.tm_sec = 0;
        this->viewType = this->config_->lView();
        switch (this->viewType) {
            case ViewPeriod::Year:
                this->tm.tm_mon = 0;

            case ViewPeriod::Month:
                this->tm.tm_mday = 1;

            default:
                break;
        }
        this->tmCopy = this->tm;
        this->viewTypeCopy = this->viewType;

        // Populate users vector
        if (!this->isUserPage_) {
            this->users = Utils::NX::getUserObjects();
        }
        this->userIdx = 0;

        // Populate titles vector
        this->titles = Utils::NX::getTitleObjects(this->users);
        this->titleIdx = 0;

        // Create Aether instance
        this->display = new Aether::Display();
        // this->display->setShowFPS(true);

        // Create overlays
        this->dtpicker = nullptr;
        this->periodpicker = new Aether::PopupList("common.view.heading"_lang);
        this->periodpicker->setBackLabel("common.buttonHint.back"_lang);
        this->periodpicker->setOKLabel("common.buttonHint.ok"_lang);

        // Setup screens
        this->setDisplayTheme();
        this->createReason = ScreenCreate::Normal;
        this->createScreens();
        this->reinitScreens_ = ReinitState::False;

        if (this->isUserPage_) {
            // Skip UserSelect screen if launched via user page
            this->setScreen(this->config_->lScreen());
        } else {
            // Start with UserSelect screen
            this->display->setFadeIn();
            this->display->setFadeOut();
            this->setScreen(ScreenID::UserSelect);
        }
    }

    void Application::checkForUpdate() {
        if (Utils::Update::needsCheck()) {
            Utils::Update::check();
        }

        this->hasUpdate_ = Utils::Update::available();
    }

    void Application::reinitScreens(ScreenCreate c) {
        this->createReason = c;
        this->reinitScreens_ = ReinitState::Wait;
    }

    void Application::createScreens() {
        this->scAllActivity = new Screen::AllActivity(this);
        this->scCustomTheme = new Screen::CustomTheme(this);
        this->scDetails = new Screen::Details(this);
        this->scRecentActivity = new Screen::RecentActivity(this);
        this->scSettings = new Screen::Settings(this, this->createReason);
        this->scUpdate = new Screen::Update(this);

        // These screens aren't used on the user page so no point wasting memory
        if (!this->isUserPage_) {
            this->scUserSelect = new Screen::UserSelect(this, this->users);
        }

        this->createReason = ScreenCreate::Normal;
    }

    void Application::deleteScreens() {
        delete this->scAllActivity;
        delete this->scCustomTheme;
        delete this->scDetails;
        delete this->scRecentActivity;
        delete this->scSettings;
        delete this->scUpdate;

        if (!this->isUserPage_) {
            delete this->scUserSelect;
        }
    }

    void Application::setHoldDelay(int i) {
        this->display->setHoldDelay(i);
    }

    void Application::addOverlay(Aether::Overlay * o) {
        this->display->addOverlay(o);
    }

    void Application::setScreen(ScreenID s) {
        switch (s) {
            case AllActivity:
                this->display->setScreen(this->scAllActivity);
                this->screen = AllActivity;
                break;

            case CustomTheme:
                this->display->setScreen(this->scCustomTheme);
                this->screen = CustomTheme;
                break;

            case Details:
                this->display->setScreen(this->scDetails);
                this->screen = Details;
                break;

            case RecentActivity:
                this->display->setScreen(this->scRecentActivity);
                this->screen = RecentActivity;
                break;

            case Settings:
                this->display->setScreen(this->scSettings);
                this->screen = Settings;
                break;

            case Update:
                this->display->setScreen(this->scUpdate);
                this->screen = Update;
                break;

            case UserSelect:
                this->display->setScreen(this->scUserSelect);
                this->screen = UserSelect;
                break;
        }
    }

    void Application::pushScreen() {
        this->display->pushScreen();
        this->screenStack.push(this->screen);

        // I'll add the onPush()/onPop() methods to all screen when I refactor them
        if (this->screen == RecentActivity) {
            this->scRecentActivity->onPush();
        }
    }

    void Application::popScreen() {
        this->display->popScreen();
        if (!this->screenStack.empty()) {
            this->screen = this->screenStack.top();
            this->screenStack.pop();

            // I'll add the onPush()/onPop() methods to all screen when I refactor them
            if (this->screen == RecentActivity) {
                this->scRecentActivity->onPop();
            }
        }
    }

    void Application::decreaseDate() {
        // All prevent going before 2000
        switch (this->viewType) {
            case ViewPeriod::Day:
                if (this->tm.tm_year == 100 && this->tm.tm_mon == 0 && this->tm.tm_mday == 1) {
                    return;
                }
                this->tm = Utils::Time::decreaseTm(this->tm, 'D');
                break;

            case ViewPeriod::Month:
                if (this->tm.tm_year == 100 && this->tm.tm_mon == 0) {
                    return;
                }
                this->tm = Utils::Time::decreaseTm(this->tm, 'M');
                break;

            case ViewPeriod::Year:
                if (this->tm.tm_year == 100) {
                    return;
                }
                this->tm = Utils::Time::decreaseTm(this->tm, 'Y');
                break;

            default:
                break;
        }
    }

    void Application::increaseDate() {
        // All prevent going past 2060 (same as switch)
        switch (this->viewType) {
            case ViewPeriod::Day:
                if (this->tm.tm_year == 160 && this->tm.tm_mon == 11 && this->tm.tm_mday == 31) {
                    return;
                }
                this->tm = Utils::Time::increaseTm(this->tm, 'D');
                break;

            case ViewPeriod::Month:
                if (this->tm.tm_year == 160 && this->tm.tm_mon == 11) {
                    return;
                }
                this->tm = Utils::Time::increaseTm(this->tm, 'M');
                break;

            case ViewPeriod::Year:
                if (this->tm.tm_year == 160) {
                    return;
                }
                this->tm = Utils::Time::increaseTm(this->tm, 'Y');
                break;

            default:
                break;
        }
    }

    void Application::createDatePicker() {
        if (this->dtpicker != nullptr) {
            delete this->dtpicker;
        }
        switch (this->viewType) {
            case ViewPeriod::Day:
                this->dtpicker = new Aether::DateTime("common.datePanel.headingDate"_lang, this->tm, Aether::DTFlag::Date);
                break;

            case ViewPeriod::Month:
                this->dtpicker = new Aether::DateTime("common.datePanel.headingMonth"_lang, this->tm, Aether::DTFlag::Month | Aether::DTFlag::Year);
                break;

            case ViewPeriod::Year:
                this->dtpicker = new Aether::DateTime("common.datePanel.headingYear"_lang, this->tm, Aether::DTFlag::Year);
                break;

            default:
                break;
        }
        this->dtpicker->setDayHint("common.datePanel.day"_lang);
        this->dtpicker->setMonthHint("common.datePanel.month"_lang);
        this->dtpicker->setYearHint("common.datePanel.year"_lang);
        this->dtpicker->setBackLabel("common.buttonHint.back"_lang);
        this->dtpicker->setOKLabel("common.buttonHint.ok"_lang);
        this->dtpicker->setAllColours(this->theme_->altBG(), this->theme_->accent(), this->theme_->mutedText(), this->theme_->mutedText(), this->theme_->text());
        this->addOverlay(this->dtpicker);
    }

    void Application::createPeriodPicker() {
        this->periodpicker->removeEntries();
        this->periodpicker->addEntry(toString(ViewPeriod::Day), [this](){
            if (this->viewType != ViewPeriod::Day) {
                // Set to current day if within range
                struct tm now = Utils::Time::getTmForCurrentTime();
                if ((this->viewType == ViewPeriod::Year && this->tm.tm_year == now.tm_year) || (this->viewType == ViewPeriod::Month && this->tm.tm_mon == now.tm_mon && this->tm.tm_year == now.tm_year)) {
                    this->tm.tm_mon = now.tm_mon;
                    this->tm.tm_mday = now.tm_mday;
                }

                this->viewType = ViewPeriod::Day;
            }
        }, this->viewType == ViewPeriod::Day);
        this->periodpicker->addEntry(toString(ViewPeriod::Month), [this](){
            if (this->viewType != ViewPeriod::Month) {
                // Set to current month if within range
                struct tm now = Utils::Time::getTmForCurrentTime();
                if (this->viewType == ViewPeriod::Year && this->tm.tm_year == now.tm_year) {
                    this->tm.tm_mon = now.tm_mon;
                }

                this->tm.tm_mday = 1;
                this->viewType = ViewPeriod::Month;
            }
        }, this->viewType == ViewPeriod::Month);
        this->periodpicker->addEntry(toString(ViewPeriod::Year), [this](){
            if (this->viewType != ViewPeriod::Year) {
                this->tm.tm_mon = 0;
                this->tm.tm_mday = 1;
                this->viewType = ViewPeriod::Year;
            }
        }, this->viewType == ViewPeriod::Year);
        this->addOverlay(this->periodpicker);
    }

    Config * Application::config() {
        return this->config_;
    }

    NX::PlayData * Application::playdata() {
        return this->playdata_;
    }

    Theme * Application::theme() {
        return this->theme_;
    }

    void Application::setDisplayTheme() {
        this->periodpicker->setBackgroundColour(this->theme_->altBG());
        this->periodpicker->setTextColour(this->theme_->text());
        this->periodpicker->setLineColour(this->theme_->fg());
        this->periodpicker->setHighlightColour(this->theme_->accent());
        this->periodpicker->setListLineColour(this->theme_->mutedLine());
        this->display->setHighlightColours(this->theme_->highlightBG(), this->theme_->selected());
        this->display->setHighlightAnimation(this->theme_->highlightFunc());
        if (this->config_->tImage() && this->config_->gTheme() == Custom) {
            if (this->display->setBackgroundImage(BACKGROUND_IMAGE)) {
                return;
            } else {
                // Turn off background image
                this->config_->setTImage(false);
            }
        }
        this->display->setBackgroundColour(this->theme_->bg().r, this->theme_->bg().g, this->theme_->bg().b);
    }

    bool Application::hasUpdate() {
        return this->hasUpdate_;
    }

    struct tm Application::time() {
        return this->tm;
    }

    ViewPeriod Application::viewPeriod() {
        return this->viewType;
    }

    bool Application::timeChanged() {
        bool b = Utils::Time::areDifferentDates(this->tm, this->tmCopy);
        if (!b) {
            b = (this->viewType != this->viewTypeCopy);
        }
        this->tmCopy = this->tm;
        this->viewTypeCopy = this->viewType;

        return b;
    }

    NX::User * Application::activeUser() {
        return this->users[this->userIdx];
    }

    bool Application::isUserPage() {
        return this->isUserPage_;
    }

    void Application::setActiveUser(unsigned short i) {
        this->userIdx = i;
    }

    std::vector<NX::Title *> Application::titleVector() {
        return this->titles;
    }

    NX::Title * Application::activeTitle() {
        return this->titles[this->titleIdx];
    }

    void Application::setActiveTitle(unsigned int i) {
        this->titleIdx = i;
    }

    void Application::run() {
        // Do main loop
        while (this->display->loop()) {
            // Check if screens should be recreated
            if (this->reinitScreens_ == ReinitState::Wait) {
                this->reinitScreens_ = ReinitState::True;
            } else if (this->reinitScreens_ == ReinitState::True) {
                this->reinitScreens_ = ReinitState::False;
                this->display->dropScreen();
                this->deleteScreens();
                this->setDisplayTheme();
                this->createScreens();
                this->setScreen(this->screen);
            }
        }
    }

    void Application::exit() {
        this->display->exit();
    }

    Application::~Application() {
        // Delete user objects
        while (users.size() > 0) {
            delete users[0];
            users.erase(users.begin());
        }

        // Delete title objects
        while (titles.size() > 0) {
            delete titles[0];
            titles.erase(titles.begin());
        }

        // Delete objects
        delete this->config_;
        delete this->playdata_;
        delete this->theme_;

        // Delete overlays
        if (this->dtpicker != nullptr) {
            delete this->dtpicker;
        }
        delete this->periodpicker;

        // Cleanup Aether
        this->deleteScreens();
        if (!this->isUserPage_) {
            // Don't delete display as it flickers black (hopefully there's a better way around this)
            delete this->display;
        }

        // Stop all services
        Utils::Curl::exit();
        Utils::NX::stopServices();

        // Install update if present
        Utils::Update::install();

        if (this->isUserPage_) {
            appletRequestExitToSelf();
        }
    }
};