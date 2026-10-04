#include <Geode/Geode.hpp>
#include <Geode/ui/Layout.hpp>
#include <arc/time/Sleep.hpp>

using namespace geode::prelude;

using SuccessFn = std::function<void()>;
using SubmitFn = std::function<bool(std::string const&)>;

#define PIN_SIZE_LIMIT 32
#define CAN_PLAY_LEVEL (!(safetylock::locked && !safetylock::isAllowed(this->m_level)))
#define CAN_USE_EDITOR (!(safetylock::locked && safetylock::m_protectEditor))
#define CAN_VIEW_COMMENTS (!(safetylock::locked && safetylock::m_protectComments))
#define CAN_MODIFY_ACCOUNT (!(safetylock::locked && safetylock::m_protectAccount))
#define CAN_CLONE_LEVEL (CAN_USE_EDITOR && CAN_PLAY_LEVEL)
#define COND_SUDO_EXIT_IFNOT(CAN) \
    if (!(CAN)) \
    { \
        if (pinSudoCheck( \
                [this] \
                { \
                    this->applyLocks(); \
                } \
            )) \
        { \
            return; \
        } \
    }
#define COND_SUDO_COND_EXIT_IFNOT(COND, COND2) \
    if (!(COND)) \
    { \
        if ((COND2)) \
        { \
            return; \
        } \
        else \
        { \
            if (pinSudoCheck( \
                    [this] \
                    { \
                        this->applyLocks(); \
                    } \
                )) \
            { \
                return; \
            } \
        } \
    }

void refreshAllScenes();

namespace safetylock
{
std::string m_pinSalt;
std::string m_pinHash;
bool m_silent;
bool m_lockOnStartup;
bool m_protectLevels;
bool m_levelNeedsStars;
bool m_levelNeedsRarity;
bool m_protectEditor;
bool m_protectComments;
bool m_protectAccount;
int m_orLevelNeedsMinimumLikes;

bool locked = false;

bool isAllowed(GJGameLevel* level)
{
    if (!level)
    {
        return false;
    }

    if (!m_protectLevels)
    {
        return true;
    }

    if (m_orLevelNeedsMinimumLikes != -1 && level->m_likes >= m_orLevelNeedsMinimumLikes)
    {
        return true;
    }

    if (m_levelNeedsStars && level->m_stars == 0)
    {
        return false;
    }

    if (m_levelNeedsRarity && level->m_isEpic == 0)
    {
        return false;
    }

    if (m_orLevelNeedsMinimumLikes != -1 && !m_levelNeedsStars && !m_levelNeedsRarity)
    {
        return false;
    }

    return true;
}

void init();
} // namespace safetylock

class PinPopup : public geode::Popup
{
protected:
    SuccessFn m_onSuccess;
    SubmitFn m_onSubmit;
    std::string m_error;

    bool init(std::string title, SuccessFn onSuccess, SubmitFn onSubmit)
    {
        if (!Popup::init(180.f, 150.f))
        {
            return false;
        }

        m_onSuccess = std::move(onSuccess);
        m_onSubmit = std::move(onSubmit);
        this->setTitle(title);

        auto container = CCNode::create();
        container->setContentSize({ 160.f, 100.f });
        container->setAnchorPoint({ 0.5f, 0.5f });
        container->setLayout(SimpleColumnLayout::create()->setGap(6.f)->setMainAxisAlignment(MainAxisAlignment::Center));

        auto input = TextInput::create(104.f, "pin");
        input->setPasswordMode(true);
        input->setMaxCharCount(32);

        auto submit = Button::createWithNode(
            ButtonSprite::create("Submit"),
            [this, input, container](Button*)
            {
                bool valid = m_onSubmit(input->getString());
                if (valid)
                {
                    auto onSuccess = std::move(m_onSuccess);
                    this->onClose(nullptr);
                    if (onSuccess)
                    {
                        onSuccess();
                    }
                    return;
                }
            }
        );

        container->addChild(input);
        container->addChild(submit);
        container->updateLayout();

        m_mainLayer->addChildAtPosition(container, Anchor::Center, { 0.f, -10.f });

        return true;
    }

public:
    static bool defaultSubmit(std::string const& pw)
    {
        if (pw.length() <= 2)
        {
            Notification::create("PIN too short", NotificationIcon::Error)->show();
            return false;
        }
        if (pw.length() > PIN_SIZE_LIMIT)
        {
            Notification::create("PIN too long", NotificationIcon::Error)->show();
            return false;
        }

        auto salt = safetylock::m_pinSalt;
        auto hash = safetylock::m_pinHash;
        bool valid = !hash.empty() && sha256(salt + pw).toString() == hash;
        if (valid)
        {
            Notification::create("PIN valid", NotificationIcon::Success)->show();
            return true;
        }
        else
        {
            Notification::create("PIN invalid", NotificationIcon::Error)->show();
            return false;
        }
    }

    static bool check(
        std::string title = "Safety Lock",
        SuccessFn onSuccess = [] {},
        SubmitFn onSubmit = defaultSubmit,
        bool ignoreLockedStatus = false
    )
    {
        if (!ignoreLockedStatus && !safetylock::locked)
        {
            onSuccess();
            return false;
        }

        auto ret = new PinPopup();
        if (ret->init(title, onSuccess, onSubmit))
        {
            ret->autorelease();
            ret->show();
            return true;
        }

        delete ret;
        return true;
    }
};

#include <charconv>

class ManagePopup : public geode::Popup
{
protected:
    struct ToggleEntry
    {
        char const* label;
        char const* key;
        bool* value;
    };

    std::vector<ToggleEntry> m_toggles;
    ButtonSprite* m_lockSprite = nullptr;

    void onToggle(CCObject* sender)
    {
        auto toggler = static_cast<CCMenuItemToggler*>(sender);
        auto& entry = m_toggles[toggler->getTag()];

        *entry.value = !toggler->isToggled();
        Mod::get()->setSavedValue(entry.key, *entry.value);
    }

    bool init()
    {
        if (!Popup::init(340.f, 220.f))
        {
            return false;
        }

        this->setTitle("Safety Lock");

        auto size = m_mainLayer->getContentSize();

        m_toggles = {
            { "Silent", "silent", &safetylock::m_silent },
            { "Protect levels", "protect-levels", &safetylock::m_protectLevels },
            { "Level needs stars", "level-needs-stars", &safetylock::m_levelNeedsStars },
            { "Level needs rarity", "level-needs-rarity", &safetylock::m_levelNeedsRarity },
            { "Lock on startup", "lock-on-startup", &safetylock::m_lockOnStartup },
            { "Protect editor", "protect-editor", &safetylock::m_protectEditor },
            { "Protect comments", "protect-comments", &safetylock::m_protectComments },
            { "Protect account", "protect-account", &safetylock::m_protectAccount },
        };

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        menu->setContentSize(size);
        m_mainLayer->addChild(menu);

        float const labelX[2] = { 20.f, 185.f };
        float const toggleX[2] = { 150.f, 315.f };
        float const top = size.height - 50.f;

        for (size_t i = 0; i < m_toggles.size(); i++)
        {
            size_t col = i / 4;
            size_t row = i % 4;
            float y = top - row * 26.f;

            auto label = geode::Label::create(m_toggles[i].label, "bigFont.fnt");
            label->setAnchorPoint({ 0.f, 0.5f });
            label->setLimitLabelWidth(110.f, 0.4f, 0.2f);
            label->setPosition({ labelX[col], y });
            m_mainLayer->addChild(label);

            auto toggler = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ManagePopup::onToggle), 0.6f);
            toggler->setTag(static_cast<int>(i));
            toggler->toggle(*m_toggles[i].value);
            toggler->setPosition({ toggleX[col], y });
            menu->addChild(toggler);
        }

        {
            float y = 65.f;

            auto label = geode::Label::create("OR Level needs minimum likes", "bigFont.fnt");
            label->setAnchorPoint({ 0.f, 0.5f });
            label->setLimitLabelWidth(200.f, 0.4f, 0.2f);
            label->setPosition({ 20.f, y });
            m_mainLayer->addChild(label);

            auto likes = TextInput::create(90.f, "off");
            likes->setFilter("0123456789");
            likes->setMaxCharCount(9);
            if (safetylock::m_orLevelNeedsMinimumLikes != -1)
            {
                likes->setString(std::to_string(safetylock::m_orLevelNeedsMinimumLikes));
            }
            likes->setCallback(
                [](std::string const& str)
                {
                    int v = -1;
                    std::from_chars(str.data(), str.data() + str.size(), v);
                    safetylock::m_orLevelNeedsMinimumLikes = v;
                    Mod::get()->setSavedValue("or-level-needs-minimum-likes", v);
                }
            );
            m_mainLayer->addChildAtPosition(likes, Anchor::BottomLeft, { 285.f, y });
        }

        {
            float y = 28.f;

            auto input = TextInput::create(200.f, "new pin");
            input->setPasswordMode(true);
            input->setMaxCharCount(32);
            m_mainLayer->addChildAtPosition(input, Anchor::BottomLeft, { 125.f, y });

            auto set = Button::createWithNode(
                ButtonSprite::create("Set"),
                [input](Button*)
                {
                    std::string pin = input->getString();
                    if (pin.length() <= 2)
                    {
                        Notification::create("PIN too short", NotificationIcon::Error)->show();
                        return;
                    }
                    if (pin.length() > PIN_SIZE_LIMIT)
                    {
                        Notification::create("PIN too long", NotificationIcon::Error)->show();
                        return;
                    }

                    safetylock::m_pinSalt = random::generateAlphanumericString(12);
                    safetylock::m_pinHash = sha256(safetylock::m_pinSalt + pin).toString();
                    Mod::get()->setSavedValue("pin-salt", safetylock::m_pinSalt);
                    Mod::get()->setSavedValue("pin-hash", safetylock::m_pinHash);

                    input->setString("");
                    Notification::create("PIN updated", NotificationIcon::Success)->show();
                }
            );
            m_mainLayer->addChildAtPosition(set, Anchor::BottomLeft, { 280.f, y });
        }

        {
            m_lockSprite = ButtonSprite::create(safetylock::locked ? "Unlock" : "Lock", 60, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f);

            auto lockButton = Button::createWithNode(
                m_lockSprite,
                [this](Button*)
                {
                    safetylock::locked = !safetylock::locked;
                    refreshAllScenes();

                    m_lockSprite->setString(safetylock::locked ? "Unlock" : "Lock");

                    Notification::create(
                        safetylock::locked ? "Locked" : "Unlocked", safetylock::locked ? NotificationIcon::Success : NotificationIcon::Info
                    )
                        ->show();
                }
            );

            m_mainLayer->addChildAtPosition(lockButton, Anchor::TopRight, { -45.f, -22.f });
        }

        return true;
    }

public:
    static ManagePopup* create()
    {
        auto ret = new ManagePopup();
        if (ret->init())
        {
            ret->autorelease();
            return ret;
        }

        delete ret;
        return nullptr;
    }
};

void safetylock::init()
{
    safetylock::m_pinSalt = Mod::get()->getSavedValue<std::string>("pin-salt", "");
    safetylock::m_pinHash = Mod::get()->getSavedValue<std::string>("pin-hash", "");
    safetylock::m_silent = Mod::get()->getSavedValue<bool>("silent", true);
    safetylock::m_lockOnStartup = Mod::get()->getSavedValue<bool>("lock-on-startup", true);
    safetylock::m_protectLevels = Mod::get()->getSavedValue<bool>("protect-levels", true);
    safetylock::m_levelNeedsStars = Mod::get()->getSavedValue<bool>("level-needs-stars", true);
    safetylock::m_levelNeedsRarity = Mod::get()->getSavedValue<bool>("level-needs-rarity", false);
    safetylock::m_protectEditor = Mod::get()->getSavedValue<bool>("protect-editor", true);
    safetylock::m_protectComments = Mod::get()->getSavedValue<bool>("protect-comments", true);
    safetylock::m_protectAccount = Mod::get()->getSavedValue<bool>("protect-account", true);
    safetylock::m_orLevelNeedsMinimumLikes = Mod::get()->getSavedValue<int>("or-level-needs-minimum-likes", 100);

    safetylock::locked = false;
    if (!safetylock::m_pinHash.empty())
    {
        if (safetylock::m_lockOnStartup)
        {
            safetylock::locked = true;
        }
    }

    ButtonSettingPressedEventV3(Mod::get(), "lock")
        .listen(
            [](auto buttonKey)
            {
                if (buttonKey == "manage-lock")
                {
                    if (safetylock::m_pinHash.empty())
                    {
                        PinPopup::check(
                            "Setup PIN",
                            []
                            {
                                ManagePopup::create()->show();
                            },
                            [](std::string const& pin)
                            {
                                if (pin.length() <= 2)
                                {
                                    Notification::create("PIN too short", NotificationIcon::Error)->show();
                                    return false;
                                }
                                if (pin.length() > PIN_SIZE_LIMIT)
                                {
                                    Notification::create("PIN too long", NotificationIcon::Error)->show();
                                    return false;
                                }

                                safetylock::m_pinSalt = random::generateAlphanumericString(12);
                                safetylock::m_pinHash = sha256(safetylock::m_pinSalt + pin).toString();
                                Mod::get()->setSavedValue("pin-salt", safetylock::m_pinSalt);
                                Mod::get()->setSavedValue("pin-hash", safetylock::m_pinHash);

                                safetylock::locked = true;

                                Notification::create("PIN created", NotificationIcon::Success)->show();
                                return true;
                            },
                            true
                        );
                    }
                    else
                    {
                        PinPopup::check(
                            "Enter PIN",
                            []
                            {
                                ManagePopup::create()->show();
                            }
                        );
                    }
                }
            }
        )
        .leak();
}

$on_mod(Loaded)
{
    safetylock::init();
}

bool pinSudoCheck(SuccessFn onSuccess = [] {})
{
    return PinPopup::check(
        "Enter PIN",
        [onSuccess]
        {
            safetylock::locked = false;
            refreshAllScenes();
            Notification::create("Unlocked", NotificationIcon::Info)->show();
            onSuccess();
        }
    );
}

void setButtonLocked(CCMenuItemSpriteExtra* btn, bool unlocked)
{
    if (!btn)
    {
        return;
    }

    auto img = typeinfo_cast<CCNodeRGBA*>(btn->getNormalImage());
    if (!img)
    {
        return;
    }

    auto existing = img->getChildByID("lock-icon"_spr);

    if (!unlocked)
    {
        img->setCascadeColorEnabled(true);
        img->setColor({ 50, 50, 50 });

        if (!existing)
        {
            if (auto lock = CCSprite::createWithSpriteFrameName("GJ_lockGray_001.png"))
            {
                lock->setID("lock-icon"_spr);
                lock->setScale(1.2f);
                lock->setPosition(img->getContentSize() / 2);
                img->addChild(lock);
            }
        }
    }
    else if (existing)
    {
        img->setColor({ 255, 255, 255 });
        existing->removeFromParent();
    }
}

void setButtonLocked(CCNode* root, char const* id, bool unlocked)
{
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(root->getChildByIDRecursive(id));
    if (!btn)
    {
        return;
    }

    setButtonLocked(btn, unlocked);
}

void setVisibleByUs(CCNode* node, bool visible)
{
    if (!node)
    {
        return;
    }

    auto key = "hidden"_spr;
    if (!visible)
    {
        if (node->isVisible())
        {
            node->setVisible(false);
            node->setUserObject(key, CCBool::create(true));
        }
    }
    else if (node->getUserObject(key))
    {
        node->setVisible(true);
        node->setUserObject(key, nullptr);
    }
}


void setEnabledByUs(CCMenuItem* node, bool enabled)
{
    if (!node)
    {
        return;
    }

    auto key = "disabled"_spr;
    if (!enabled)
    {
        if (node->isEnabled())
        {
            node->setEnabled(false);
            node->setUserObject(key, CCBool::create(true));
        }
    }
    else if (node->getUserObject(key))
    {
        node->setEnabled(true);
        node->setUserObject(key, nullptr);
    }
}

#include <Geode/modify/MenuLayer.hpp>
class $modify(SafetyLockMenuLayer, MenuLayer)
{
    bool init()
    {
        if (!MenuLayer::init())
        {
            return false;
        }

        if (!Mod::get()->setSavedValue<bool>("seen-first-launch-warning", true))
        {
            this->runAction(
                CCSequence::create(
                    CCDelayTime::create(0.5f), CCCallFunc::create(this, callfunc_selector(SafetyLockMenuLayer::showFirstLaunch)), nullptr
                )
            );
        }

        return true;
    }

    void showFirstLaunch()
    {
        Mod::get()->setSavedValue("seen-first-launch-warning", true);
        FLAlertLayer::create("Safety Lock", "You need to configure Safety Lock for it to do anything.\nYou will only see this once.", "OK")->show();
    }
};

#include <Geode/modify/OptionsLayer.hpp>
class $modify(SafetyLockOptionsLayer, OptionsLayer)
{
    bool init(const char* title)
    {
        if (!OptionsLayer::init(title))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void customSetup()
    {
        OptionsLayer::customSetup();
        this->applyLocks();
    }

    void applyLocks()
    {
        auto optionsMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("options-menu"));
        if (optionsMenu)
        {
            auto accountBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(optionsMenu->getChildByIDRecursive("account-button"));
            if (accountBtn)
            {
                if (safetylock::m_silent)
                {
                    setVisibleByUs(accountBtn, CAN_MODIFY_ACCOUNT);
                }
                else
                {
                    if (auto bs = typeinfo_cast<ButtonSprite*>(accountBtn->getNormalImage()))
                    {
                        if (auto label = bs->m_label)
                        {
                            setVisibleByUs(label, CAN_MODIFY_ACCOUNT);
                        }
                    }
                    setButtonLocked(accountBtn, CAN_MODIFY_ACCOUNT);
                }
            }

            optionsMenu->updateLayout();
        }
    }

    void onAccount(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        OptionsLayer::onAccount(sender);
    }
};

#include <Geode/modify/ProfilePage.hpp>
class $modify(SafetyLockProfilePage, ProfilePage)
{
    bool init(int accountID, bool ownProfile)
    {
        if (!ProfilePage::init(accountID, ownProfile))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        auto commentList = typeinfo_cast<GJCommentListLayer*>(this->getChildByIDRecursive("GJCommentListLayer"));
        if (commentList)
        {
            setVisibleByUs(commentList, CAN_VIEW_COMMENTS);
        }

        auto bottomMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("bottom-menu"));
        if (bottomMenu)
        {
            for (auto id : { "message-button", "friend-button", "requests-button", "settings-button", "block-button", "follow-button" })
            {
                auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(bottomMenu->getChildByIDRecursive(id));

                if (safetylock::m_silent)
                {
                    setVisibleByUs(btn, CAN_MODIFY_ACCOUNT);
                }
                else
                {
                    setButtonLocked(btn, CAN_MODIFY_ACCOUNT);
                }
            }

            bottomMenu->updateLayout();
        }

        auto leftMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("left-menu"));
        if (leftMenu)
        {
            auto commentsHistoryBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("comment-history-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(commentsHistoryBtn, CAN_VIEW_COMMENTS);
            }
            else
            {
                setButtonLocked(commentsHistoryBtn, CAN_VIEW_COMMENTS);
            }

            leftMenu->updateLayout();
        }

        auto mainMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("main-menu"));
        if (mainMenu)
        {
            auto commentBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(mainMenu->getChildByIDRecursive("comment-button"));
            auto followBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(mainMenu->getChildByIDRecursive("follow-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(commentBtn, CAN_MODIFY_ACCOUNT);
                setVisibleByUs(followBtn, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(commentBtn, CAN_MODIFY_ACCOUNT);
                setButtonLocked(followBtn, CAN_MODIFY_ACCOUNT);
            }
        }

        auto followHint = typeinfo_cast<CCSprite*>(this->getChildByIDRecursive("follow-hint"));
        if (followHint)
        {
            if (safetylock::m_silent)
            {
                setVisibleByUs(followHint, CAN_MODIFY_ACCOUNT);
            }
        }
    }

    void loadPage(int page)
    {
        ProfilePage::loadPage(page);
        this->applyLocks();
    }

    void loadPageFromUserInfo(GJUserScore* score)
    {
        ProfilePage::loadPageFromUserInfo(score);
        this->applyLocks();
    }

    void loadCommentsFinished(cocos2d::CCArray* comments, char const* key)
    {
        ProfilePage::loadCommentsFinished(comments, key);
        this->applyLocks();
    }

    void onComment(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onComment(sender);
    }

    void onBlockUser(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onBlockUser(sender);
    }

    void onFollow(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onFollow(sender);
    }

    void onFriend(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onFriend(sender);
    }

    void onFriends(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onFriends(sender);
    }

    void onRequests(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onRequests(sender);
    }

    void onSendMessage(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onSendMessage(sender);
    }

    void onMessages(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onMessages(sender);
    }

    void onSettings(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        ProfilePage::onSettings(sender);
    }

    void onCommentHistory(cocos2d::CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_VIEW_COMMENTS)
        ProfilePage::onCommentHistory(sender);
    }
};

#include <Geode/modify/CommentCell.hpp>
class $modify(SafetyLockCommentCell, CommentCell)
{
    bool init()
    {
        if (!CommentCell::init())
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("delete-button"));
        auto likeBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("like-button"));
        if (safetylock::m_silent)
        {
            setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
        }
        else
        {
            setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
        }
        setEnabledByUs(likeBtn, CAN_MODIFY_ACCOUNT);
    }

    void loadFromComment(GJComment* comment)
    {
        CommentCell::loadFromComment(comment);
        this->applyLocks();
    }

    void onConfirmDelete(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        CommentCell::onConfirmDelete(sender);
    }
};

#include <Geode/modify/LevelInfoLayer.hpp>
class $modify(SafetyLockLevelInfoLayer, LevelInfoLayer)
{
    bool init(GJGameLevel* level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void levelDownloadFinished(GJGameLevel* level)
    {
        LevelInfoLayer::levelDownloadFinished(level);
        this->applyLocks();
    }

    void applyLocks()
    {
        if (this->m_level)
        {
            auto playBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("play-button"));
            auto favoriteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("favorite-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(playBtn, CAN_PLAY_LEVEL);
                setVisibleByUs(favoriteBtn, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(playBtn, CAN_PLAY_LEVEL);
                setButtonLocked(favoriteBtn, CAN_MODIFY_ACCOUNT);
            }

            auto leftSideMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("left-side-menu"));
            if (leftSideMenu)
            {
                auto copyBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(leftSideMenu->getChildByIDRecursive("copy-button"));
                auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(leftSideMenu->getChildByIDRecursive("delete-button"));

                if (safetylock::m_silent)
                {
                    setVisibleByUs(copyBtn, CAN_CLONE_LEVEL);
                    setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
                }
                else
                {
                    setButtonLocked(copyBtn, CAN_CLONE_LEVEL);
                    setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
                }

                leftSideMenu->updateLayout();
            }

            auto rightSideMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("right-side-menu"));
            if (rightSideMenu)
            {
                auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("delete-button"));
                auto infoBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("info-button"));
                auto likeBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("like-button"));
                auto rateBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("rate-button"));

                if (safetylock::m_silent)
                {
                    setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
                    setVisibleByUs(infoBtn, CAN_VIEW_COMMENTS);
                    setVisibleByUs(likeBtn, CAN_MODIFY_ACCOUNT);
                    setVisibleByUs(rateBtn, CAN_MODIFY_ACCOUNT);
                }
                else
                {
                    setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
                    setButtonLocked(infoBtn, CAN_VIEW_COMMENTS);
                    setButtonLocked(likeBtn, CAN_MODIFY_ACCOUNT);
                    setButtonLocked(rateBtn, CAN_MODIFY_ACCOUNT);
                }

                if (auto layout = typeinfo_cast<AxisLayout*>(rightSideMenu->getLayout()))
                {
                    layout->ignoreInvisibleChildren(true);
                }
                rightSideMenu->updateLayout();
            }
        }
    }

    void onPlay(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_PLAY_LEVEL)
        LevelInfoLayer::onPlay(sender);
    }

    void confirmClone(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_CLONE_LEVEL)
        LevelInfoLayer::confirmClone(sender);
    }

    void tryCloneLevel(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_CLONE_LEVEL)
        LevelInfoLayer::tryCloneLevel(sender);
    }

    void confirmDelete(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::confirmDelete(sender);
    }

    void confirmOwnerDelete(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::confirmOwnerDelete(sender);
    }

    void onLike(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::onLike(sender);
    }

    void onRateDemon(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::onRateDemon(sender);
    }

    void onRateStars(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::onRateStars(sender);
    }

    void onRate(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::onRate(sender);
    }

    void onInfo(CCObject* sender)
    {
        COND_SUDO_COND_EXIT_IFNOT(CAN_VIEW_COMMENTS, safetylock::m_silent)
        LevelInfoLayer::onInfo(sender);
    }

    void onFavorite(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelInfoLayer::onFavorite(sender);
    }
};

#include <Geode/modify/InfoLayer.hpp>
class $modify(SafetyLockInfoLayer, InfoLayer)
{
    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list)
    {
        if (!InfoLayer::init(level, score, list))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        auto commentBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("comment-button"));
        auto reportBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("report-button"));
        if (safetylock::m_silent)
        {
            setVisibleByUs(commentBtn, CAN_MODIFY_ACCOUNT);
            setVisibleByUs(reportBtn, CAN_MODIFY_ACCOUNT);
        }
        else
        {
            setButtonLocked(commentBtn, CAN_MODIFY_ACCOUNT);
            setButtonLocked(reportBtn, CAN_MODIFY_ACCOUNT);
        }
    }

    void onComment(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        InfoLayer::onComment(sender);
    }

    void confirmReport(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        InfoLayer::confirmReport(sender);
    }
};

#include <Geode/modify/LevelBrowserLayer.hpp>
class $modify(SafetyLockLevelBrowserLayer, LevelBrowserLayer)
{
    bool init(GJSearchObject* object)
    {
        if (!LevelBrowserLayer::init(object))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        auto deleteMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("delete-menu"));
        if (deleteMenu)
        {
            auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(deleteMenu->getChildByIDRecursive("delete-button"));
            if (deleteBtn)
            {
                if (safetylock::m_silent)
                {
                    setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
                }
                else
                {
                    if (auto bs = typeinfo_cast<ButtonSprite*>(deleteBtn->getNormalImage()))
                    {
                        if (auto sprite = bs->m_subSprite)
                        {
                            setVisibleByUs(sprite, CAN_MODIFY_ACCOUNT);
                        }
                    }
                    setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
                }
            }
        }

        auto savedMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("saved-menu"));
        if (savedMenu)
        {
            auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(savedMenu->getChildByIDRecursive("delete-button"));
            if (safetylock::m_silent)
            {
                setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
            }
        }
    }

    void onDeleteAll(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelBrowserLayer::onDeleteAll(sender);
    }

    void onRemoveAllFavorites(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelBrowserLayer::onRemoveAllFavorites(sender);
    }

    void onDeleteSelected(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelBrowserLayer::onDeleteSelected(sender);
    }

    void setupLevelBrowser(CCArray* levels)
    {
        if (levels && safetylock::locked && safetylock::m_silent)
        {
            auto filtered = CCArray::create();
            for (auto entry : CCArrayExt<CCObject*>(levels))
            {
                auto level = typeinfo_cast<GJGameLevel*>(entry);
                if (!level || safetylock::isAllowed(level))
                {
                    filtered->addObject(entry);
                }
            }
            levels = filtered;
        }

        LevelBrowserLayer::setupLevelBrowser(levels);
    }
};

#include <Geode/modify/CreatorLayer.hpp>
class $modify(SafetyLockCreatorLayer, CreatorLayer)
{
    bool init()
    {
        if (!CreatorLayer::init())
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        if (auto creatorButtons = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("creator-buttons-menu")))
        {
            auto createBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(creatorButtons->getChildByIDRecursive("create-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(createBtn, CAN_USE_EDITOR);
            }
            else
            {
                setButtonLocked(createBtn, CAN_USE_EDITOR);
            }

            creatorButtons->updateLayout();
        }

        if (auto bottomRight = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("bottom-right-menu")))
        {

            auto treasureRoomBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(bottomRight->getChildByIDRecursive("treasure-room-button"));
            if (safetylock::m_silent)
            {
                setVisibleByUs(treasureRoomBtn, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(treasureRoomBtn, CAN_MODIFY_ACCOUNT);
            }

            bottomRight->updateLayout();
        }
    }

    void onMyLevels(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_USE_EDITOR)
        CreatorLayer::onMyLevels(sender);
    }

    void onTreasureRoom(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        CreatorLayer::onTreasureRoom(sender);
    }
};

#include <Geode/modify/LevelListLayer.hpp>
class $modify(SafetyLockLevelListLayer, LevelListLayer)
{
    bool init(GJLevelList* list)
    {
        if (!LevelListLayer::init(list))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        if (auto rightSideMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("right-side-menu")))
        {
            CCMenuItemSpriteExtra* infoButton = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("info-button"));
            CCMenuItemSpriteExtra* likeButton = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("like-button"));
            CCMenuItemSpriteExtra* copyButton = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("copy-button"));
            CCMenuItemSpriteExtra* favoriteButton = typeinfo_cast<CCMenuItemSpriteExtra*>(rightSideMenu->getChildByIDRecursive("favorite-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(infoButton, CAN_VIEW_COMMENTS);
                setVisibleByUs(likeButton, CAN_MODIFY_ACCOUNT);
                setVisibleByUs(copyButton, CAN_MODIFY_ACCOUNT);
                setVisibleByUs(favoriteButton, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(infoButton, CAN_VIEW_COMMENTS);
                setButtonLocked(likeButton, CAN_MODIFY_ACCOUNT);
                setButtonLocked(copyButton, CAN_MODIFY_ACCOUNT);
                setButtonLocked(favoriteButton, CAN_MODIFY_ACCOUNT);
            }

            if (auto layout = typeinfo_cast<AxisLayout*>(rightSideMenu->getLayout()))
            {
                layout->ignoreInvisibleChildren(true);
            }
            rightSideMenu->updateLayout();
        }
    }

    void onInfo(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_VIEW_COMMENTS)
        LevelListLayer::onInfo(sender);
    }

    void onLike(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelListLayer::onLike(sender);
    }

    void confirmClone(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelListLayer::confirmClone(sender);
    }

    void onFavorite(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        LevelListLayer::onFavorite(sender);
    }
};

#include <Geode/modify/EditLevelLayer.hpp>
class $modify(SafetyLockEditLevelLayer, EditLevelLayer)
{
    bool init(GJGameLevel* level)
    {
        if (!EditLevelLayer::init(level))
        {
            return false;
        }

        this->applyLocks();
        return true;
    }

    void applyLocks()
    {
        auto levelActionsMenu = typeinfo_cast<CCMenu*>(this->getChildByIDRecursive("level-actions-menu"));
        if (levelActionsMenu)
        {
            auto deleteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(levelActionsMenu->getChildByIDRecursive("delete-button"));

            if (safetylock::m_silent)
            {
                setVisibleByUs(deleteBtn, CAN_MODIFY_ACCOUNT);
            }
            else
            {
                setButtonLocked(deleteBtn, CAN_MODIFY_ACCOUNT);
            }

            levelActionsMenu->updateLayout();
        }
    }

    void confirmDelete(CCObject* sender)
    {
        COND_SUDO_EXIT_IFNOT(CAN_MODIFY_ACCOUNT)
        EditLevelLayer::confirmDelete(sender);
    }
};

template<class Base, class Mod>
static void refreshLayers(CCNode* scene)
{
    for (auto child : CCArrayExt<CCNode*>(scene->getChildren()))
    {
        if (auto layer = typeinfo_cast<Base*>(child))
        {
            static_cast<Mod*>(layer)->applyLocks();
        }
    }
}

static void refreshScene(CCNode* scene)
{
    if (!scene)
    {
        return;
    }

    refreshLayers<OptionsLayer, SafetyLockOptionsLayer>(scene);
    refreshLayers<ProfilePage, SafetyLockProfilePage>(scene);
    refreshLayers<LevelInfoLayer, SafetyLockLevelInfoLayer>(scene);
    refreshLayers<InfoLayer, SafetyLockInfoLayer>(scene);
    refreshLayers<LevelBrowserLayer, SafetyLockLevelBrowserLayer>(scene);
    refreshLayers<CreatorLayer, SafetyLockCreatorLayer>(scene);
    refreshLayers<LevelListLayer, SafetyLockLevelListLayer>(scene);
    refreshLayers<EditLevelLayer, SafetyLockEditLevelLayer>(scene);
}

void refreshAllScenes()
{
    auto director = CCDirector::get();
    refreshScene(director->getRunningScene());

    if (director->m_pobScenesStack)
    {
        for (auto scene : CCArrayExt<CCNode*>(director->m_pobScenesStack))
        {
            refreshScene(scene);
        }
    }
}