// iOS's launcher: a UIKit screen shown before the game's window, from which the player extracts the game data from
// their disc image, changes the APP SETTINGS and starts the game. The main thread turns the run loop here until
// Play is tapped, as the game does from its own loop later.

#include "ios_launcher.hpp"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "options/rows.hpp"
#include "platform/config.hpp"
#include "platform/firstrun.hpp"

namespace fs = std::filesystem;

namespace {

UIColor *Accent() { return [UIColor colorWithRed:0.93 green:0.74 blue:0.36 alpha:1]; }

NSString *Ns(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()] ?: @""; }

NSString *DeviceName() {
    return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? @"iPad" : @"iPhone";
}

UIButton *MakeButton(NSString *title, NSString *symbol, bool primary) {
    UIButtonConfiguration *configuration =
        primary ? UIButtonConfiguration.filledButtonConfiguration : UIButtonConfiguration.grayButtonConfiguration;
    configuration.title = title;
    configuration.image = [UIImage systemImageNamed:symbol];
    configuration.imagePadding = 8;
    configuration.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    configuration.buttonSize = UIButtonConfigurationSizeLarge;
    if (primary) {
        configuration.baseBackgroundColor = Accent();
        configuration.baseForegroundColor = UIColor.blackColor;
    } else {
        configuration.baseForegroundColor = UIColor.whiteColor;
    }
    UIButton *button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:48].active = YES;
    return button;
}

void SetTitle(UIButton *button, NSString *title, NSString *symbol) {
    UIButtonConfiguration *configuration = button.configuration;
    configuration.title = title;
    configuration.image = [UIImage systemImageNamed:symbol];
    button.configuration = configuration;
}

} // namespace

// The APP SETTINGS rows of the game's Options page (options::Pages), each with the arrows that step it as Left
// and Right do in the game. Binding rows wait for a key in the game, so they stay there.
@interface CTLauncherSettingsController : UITableViewController
@end

@implementation CTLauncherSettingsController {
    std::vector<std::pair<int, std::vector<int>>> _sections;
}

- (instancetype)init {
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) {
        self.title = @(options::kPageTitle);
        const auto pages = options::Pages();
        for (int page = 0; page < static_cast<int>(pages.size()); ++page) {
            std::vector<int> rows;
            for (int row = 0; row < static_cast<int>(pages[page].rows.size()); ++row) {
                if (pages[page].rows[row].step && pages[page].rows[row].action.empty()) {
                    rows.push_back(row);
                }
            }
            if (!rows.empty()) {
                _sections.emplace_back(page, std::move(rows));
            }
        }
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                      target:self
                                                      action:@selector(done)];
}

- (void)done {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (const options::Row &)rowAt:(NSIndexPath *)path {
    const auto &[page, rows] = _sections[path.section];
    return options::Pages()[page].rows[rows[path.row]];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return static_cast<NSInteger>(_sections.size());
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return static_cast<NSInteger>(_sections[section].second.size());
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    return @(options::Pages()[_sections[section].first].name);
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return section + 1 == static_cast<NSInteger>(_sections.size())
               ? @"In the game these are under Options: press SELECT. Changes are saved at once."
               : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"row"];
    if (cell == nil) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"row"];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    const options::Row &row = [self rowAt:path];
    UIListContentConfiguration *content = UIListContentConfiguration.subtitleCellConfiguration;
    content.text = Ns(row.label);
    content.secondaryText = Ns(row.help);
    content.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    cell.contentConfiguration = content;

    const NSInteger tag = path.section * 1000 + path.row;
    UIButton *previous = [UIButton systemButtonWithImage:[UIImage systemImageNamed:@"chevron.left"]
                                                  target:self
                                                  action:@selector(previous:)];
    UIButton *next = [UIButton systemButtonWithImage:[UIImage systemImageNamed:@"chevron.right"]
                                              target:self
                                              action:@selector(next:)];
    previous.tag = next.tag = tag;
    previous.tintColor = next.tintColor = Accent();
    UILabel *value = [UILabel new];
    value.text = Ns(row.value(ConfigGet()));
    value.textAlignment = NSTextAlignmentCenter;
    value.font = [UIFont monospacedDigitSystemFontOfSize:16 weight:UIFontWeightMedium];
    value.adjustsFontSizeToFitWidth = YES;
    value.minimumScaleFactor = 0.6;
    UIStackView *stepper = [[UIStackView alloc] initWithArrangedSubviews:@[ previous, value, next ]];
    stepper.frame = CGRectMake(0, 0, 190, 44);
    [previous.widthAnchor constraintEqualToConstant:40].active = YES;
    [next.widthAnchor constraintEqualToConstant:40].active = YES;
    cell.accessoryView = stepper;
    return cell;
}

- (void)step:(UIButton *)button direction:(int)direction {
    NSIndexPath *path = [NSIndexPath indexPathForRow:button.tag % 1000 inSection:button.tag / 1000];
    Config config = ConfigGet();
    if (!options::StepRow([self rowAt:path], config, direction)) {
        return;
    }
    if (!ConfigChange(config)) {
        UIAlertController *alert =
            [UIAlertController alertControllerWithTitle:@"Settings not saved"
                                                message:@"The change applies until the app closes, but config.json "
                                                        @"could not be written."
                                         preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
    }
    [self.tableView reloadRowsAtIndexPaths:@[ path ] withRowAnimation:UITableViewRowAnimationNone];
}

- (void)previous:(UIButton *)button {
    [self step:button direction:-1];
}

- (void)next:(UIButton *)button {
    [self step:button direction:1];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

@end

@interface CTLauncherController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic) BOOL started;
@end

@implementation CTLauncherController {
    fs::path                _root;
    std::optional<fs::path> _image;
    // A disc image chosen in Files stays readable only between these calls.
    NSURL                  *_scoped;
    NSTimer                *_timer;
    UIBackgroundTaskIdentifier _background;

    UIImageView    *_statusIcon;
    UILabel        *_statusTitle;
    UILabel        *_statusBody;
    UIProgressView *_progress;
    UILabel        *_percent;
    UIButton       *_primary;
    UIButton       *_secondary;
    UIStackView    *_buttons;
}

- (instancetype)initWithRoot:(const fs::path &)root {
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _root = root;
        _background = UIBackgroundTaskInvalid;
    }
    return self;
}

- (UILabel *)labelWithSize:(CGFloat)size weight:(UIFontWeight)weight colour:(UIColor *)colour {
    UILabel *label = [UILabel new];
    label.font = [UIFont systemFontOfSize:size weight:weight];
    label.textColor = colour;
    label.numberOfLines = 0;
    return label;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.05 green:0.06 blue:0.09 alpha:1];
    CAGradientLayer *gradient = [CAGradientLayer layer];
    gradient.colors = @[
        (id)[UIColor colorWithRed:0.16 green:0.11 blue:0.20 alpha:1].CGColor,
        (id)[UIColor colorWithRed:0.04 green:0.05 blue:0.08 alpha:1].CGColor
    ];
    gradient.startPoint = CGPointMake(0, 0);
    gradient.endPoint = CGPointMake(1, 1);
    [self.view.layer insertSublayer:gradient atIndex:0];

    // The left column: the icon, the title and what the app needs.
    UIImageView *icon = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"AppIcon60x60"]
                                                               ?: [UIImage systemImageNamed:@"gamecontroller.fill"]];
    icon.tintColor = Accent();
    icon.contentMode = UIViewContentModeScaleAspectFit;
    icon.layer.cornerRadius = 22;
    icon.layer.masksToBounds = YES;
    [icon.widthAnchor constraintEqualToConstant:96].active = YES;
    [icon.heightAnchor constraintEqualToConstant:96].active = YES;
    UILabel *title = [self labelWithSize:34 weight:UIFontWeightBold colour:UIColor.whiteColor];
    title.text = @"Dark Chronicle";
    NSString *version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"";
    UILabel *subtitle = [self labelWithSize:15 weight:UIFontWeightRegular colour:UIColor.secondaryLabelColor];
    subtitle.text = [NSString stringWithFormat:@"Dark Cloud 2 on %@ · ChronicleTwo %@", DeviceName(), version];
    UILabel *notice = [self labelWithSize:12 weight:UIFontWeightRegular colour:UIColor.tertiaryLabelColor];
    notice.text = @"Needs your own copy of the game. No game data comes with this app.";
    UIStackView *left = [[UIStackView alloc] initWithArrangedSubviews:@[ icon, title, subtitle, notice ]];
    left.axis = UILayoutConstraintAxisVertical;
    left.alignment = UIStackViewAlignmentLeading;
    left.spacing = 10;
    [left setCustomSpacing:18 afterView:icon];

    // The right column: the data's state, then what to do about it.
    _statusIcon = [UIImageView new];
    _statusIcon.contentMode = UIViewContentModeScaleAspectFit;
    [_statusIcon.widthAnchor constraintEqualToConstant:26].active = YES;
    [_statusIcon.heightAnchor constraintEqualToConstant:26].active = YES;
    _statusTitle = [self labelWithSize:18 weight:UIFontWeightSemibold colour:UIColor.whiteColor];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[ _statusIcon, _statusTitle ]];
    header.spacing = 10;
    header.alignment = UIStackViewAlignmentCenter;
    _statusBody = [self labelWithSize:15 weight:UIFontWeightRegular colour:UIColor.secondaryLabelColor];
    _progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    _progress.progressTintColor = Accent();
    _percent = [self labelWithSize:13 weight:UIFontWeightMedium colour:UIColor.secondaryLabelColor];
    _percent.font = [UIFont monospacedDigitSystemFontOfSize:13 weight:UIFontWeightMedium];
    UIStackView *card = [[UIStackView alloc] initWithArrangedSubviews:@[ header, _statusBody, _progress, _percent ]];
    card.axis = UILayoutConstraintAxisVertical;
    card.spacing = 10;
    card.layoutMarginsRelativeArrangement = YES;
    card.directionalLayoutMargins = NSDirectionalEdgeInsetsMake(16, 16, 16, 16);
    card.backgroundColor = [UIColor colorWithWhite:1 alpha:0.07];
    card.layer.cornerRadius = 16;

    _primary = MakeButton(@"Play", @"play.fill", true);
    [_primary addTarget:self action:@selector(primaryTapped) forControlEvents:UIControlEventTouchUpInside];
    _secondary = MakeButton(@"Choose Disc Image…", @"folder", false);
    [_secondary addTarget:self action:@selector(secondaryTapped) forControlEvents:UIControlEventTouchUpInside];
    UIButton *settings = MakeButton(@"Settings", @"gearshape", false);
    [settings addTarget:self action:@selector(showSettings) forControlEvents:UIControlEventTouchUpInside];
    UIButton *help = MakeButton(@"Help", @"questionmark.circle", false);
    [help addTarget:self action:@selector(showHelp) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *small = [[UIStackView alloc] initWithArrangedSubviews:@[ settings, help ]];
    small.spacing = 10;
    small.distribution = UIStackViewDistributionFillEqually;
    _buttons = [[UIStackView alloc] initWithArrangedSubviews:@[ _primary, _secondary, small ]];
    _buttons.axis = UILayoutConstraintAxisVertical;
    _buttons.spacing = 10;

    UIStackView *right = [[UIStackView alloc] initWithArrangedSubviews:@[ card, _buttons ]];
    right.axis = UILayoutConstraintAxisVertical;
    right.spacing = 16;

    // A small landscape iPhone is short: the right column scrolls rather than clip.
    UIScrollView *scroll = [UIScrollView new];
    scroll.alwaysBounceVertical = NO;
    scroll.showsVerticalScrollIndicator = NO;
    right.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:right];

    left.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:left];
    [self.view addSubview:scroll];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    NSLayoutConstraint *centred = [right.centerYAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.centerYAnchor];
    centred.priority = UILayoutPriorityDefaultLow;
    [NSLayoutConstraint activateConstraints:@[
        [left.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:32],
        [left.centerYAnchor constraintEqualToAnchor:safe.centerYAnchor],
        [left.widthAnchor constraintEqualToAnchor:safe.widthAnchor multiplier:0.38],
        [scroll.leadingAnchor constraintEqualToAnchor:left.trailingAnchor constant:32],
        [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-32],
        [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [right.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
        [right.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
        [right.topAnchor constraintGreaterThanOrEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [right.bottomAnchor constraintLessThanOrEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
        [right.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],
        [scroll.contentLayoutGuide.heightAnchor constraintGreaterThanOrEqualToAnchor:scroll.frameLayoutGuide.heightAnchor],
        centred,
    ]];

    // Back from the Files app, the disc image the player copied there is found without a tap.
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(refresh)
                                               name:UIApplicationDidBecomeActiveNotification
                                             object:nil];
    [self refresh];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.view.layer.sublayers.firstObject.frame = self.view.bounds;
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

- (void)setStatusSymbol:(NSString *)symbol colour:(UIColor *)colour title:(NSString *)title body:(NSString *)body {
    _statusIcon.image = [UIImage systemImageNamed:symbol];
    _statusIcon.tintColor = colour;
    _statusTitle.text = title;
    _statusBody.text = body;
}

// Shows what the data's state calls for: Play once it is current, otherwise the way to it.
- (void)refresh {
    if (_timer != nil) {
        return;
    }
    _image = FirstRunDiscImage();
    _progress.hidden = YES;
    _percent.hidden = YES;
    _buttons.hidden = NO;
    if (!FirstRunDataMissing(_root)) {
        [self setStatusSymbol:@"checkmark.circle.fill"
                       colour:UIColor.systemGreenColor
                        title:@"Ready to play"
                         body:@"The game data is extracted and current."];
        SetTitle(_primary, @"Play", @"play.fill");
        SetTitle(_secondary, @"Extract Again…", @"arrow.clockwise");
    } else if (_image) {
        [self setStatusSymbol:@"opticaldisc"
                       colour:Accent()
                        title:@"Disc image found"
                         body:[NSString stringWithFormat:@"%@ is in the app's folder. Extract the game data from it "
                                                         @"once to play: it takes several minutes.",
                                                         Ns(_image->filename().string())]];
        SetTitle(_primary, @"Extract Game Data", @"square.and.arrow.down");
        SetTitle(_secondary, @"Choose Another Disc Image…", @"folder");
    } else {
        [self setStatusSymbol:@"opticaldisc"
                       colour:Accent()
                        title:@"First, the game data"
                         body:[NSString stringWithFormat:
                                            @"The game plays from the data of your own Dark Chronicle disc (PS2).\n"
                                            @"1. Choose its disc image (.iso) in Files, or copy it into Files › On "
                                            @"My %@ › Dark Chronicle.\n"
                                            @"2. Extract the game data, once. The disc image is not needed after.",
                                            DeviceName()]];
        SetTitle(_primary, @"Choose Disc Image…", @"folder");
        _secondary.hidden = YES;
        return;
    }
    _secondary.hidden = NO;
}

- (void)primaryTapped {
    if (!FirstRunDataMissing(_root)) {
        self.started = YES;
    } else if (_image) {
        [self extract:*_image];
    } else {
        [self chooseImage];
    }
}

- (void)secondaryTapped {
    if (FirstRunDataMissing(_root)) {
        [self chooseImage];
        return;
    }
    UIAlertController *sheet = [UIAlertController
        alertControllerWithTitle:@"Extract the game data again?"
                         message:@"This replaces the extracted data, for example after an update asks for it. "
                                 @"Saves and settings are kept."
                  preferredStyle:UIAlertControllerStyleAlert];
    if (_image) {
        const fs::path image = *_image;
        [sheet addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"From %@",
                                                                                   Ns(image.filename().string())]
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction *) {
                                                  [self extract:image];
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Choose Disc Image…"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction *) {
                                              [self chooseImage];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:sheet animated:YES completion:nil];
}

- (void)chooseImage {
    NSMutableArray<UTType *> *types = [NSMutableArray arrayWithObject:UTTypeDiskImage];
    if (UTType *iso = [UTType typeWithFilenameExtension:@"iso"]) {
        [types insertObject:iso atIndex:0];
    }
    // Opened in place: a copy of a disc image of several gigabytes would need the space twice.
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types
                                                                                                        asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (url == nil || url.path == nil) {
        return;
    }
    if ([url startAccessingSecurityScopedResource]) {
        _scoped = url;
    }
    [self extract:fs::path(url.path.fileSystemRepresentation)];
}

- (void)extract:(const fs::path &)image {
    FirstRunExtractBegin(image, _root);
    // The screen must stay on through the extraction: a locked device suspends the app. A trip to the background
    // gets the little time iOS grants before it suspends it too.
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    _background = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Extract the game data"
                                                             expirationHandler:^{
                                                               [self endBackgroundTask];
                                                             }];
    [self setStatusSymbol:@"gearshape.2.fill"
                   colour:Accent()
                    title:[NSString stringWithFormat:@"Extracting from %@", Ns(image.filename().string())]
                     body:@"This takes several minutes. Keep the app open and the screen on."];
    _progress.hidden = NO;
    _percent.hidden = NO;
    _progress.progress = 0;
    _percent.text = @"Starting…";
    _buttons.hidden = YES;
    _timer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)endBackgroundTask {
    if (_background != UIBackgroundTaskInvalid) {
        [UIApplication.sharedApplication endBackgroundTask:_background];
        _background = UIBackgroundTaskInvalid;
    }
}

- (void)tick {
    const FirstRunExtraction extraction = FirstRunExtractPoll();
    if (extraction.running) {
        [_progress setProgress:extraction.fraction animated:YES];
        _percent.text = [NSString stringWithFormat:@"%d%%  ·  %@", static_cast<int>(extraction.fraction * 100),
                                                   Ns(extraction.stage)];
        return;
    }
    [_timer invalidate];
    _timer = nil;
    [_scoped stopAccessingSecurityScopedResource];
    _scoped = nil;
    UIApplication.sharedApplication.idleTimerDisabled = NO;
    [self endBackgroundTask];
    [self refresh];
    if (extraction.failure.empty()) {
        [[UINotificationFeedbackGenerator new] notificationOccurred:UINotificationFeedbackTypeSuccess];
        return;
    }
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"The game data could not be extracted"
                         message:[NSString stringWithFormat:@"%@\n\nCheck that the disc image is a whole Dark "
                                                            @"Chronicle (PS2) disc and that the %@ has free storage "
                                                            @"for the extracted data.",
                                                            Ns(extraction.failure), DeviceName()]
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)showSettings {
    UINavigationController *navigation =
        [[UINavigationController alloc] initWithRootViewController:[CTLauncherSettingsController new]];
    navigation.modalPresentationStyle = UIModalPresentationFormSheet;
    [self presentViewController:navigation animated:YES completion:nil];
}

- (void)showHelp {
    NSString *text = [NSString
        stringWithFormat:
            @"Dark Chronicle plays from the data of your own Dark Chronicle (PS2) disc.\n\n"
            @"1. Make a disc image (.iso) of your disc.\n"
            @"2. Put it on this %1$@: tap Choose Disc Image and pick it in Files, or copy it into Files › On My "
            @"%1$@ › Dark Chronicle.\n"
            @"3. Tap Extract Game Data. It takes several minutes; keep the app open and the screen on.\n\n"
            @"Under LiveContainer, copy the image into LiveContainer's folder: Data › Application › the game's "
            @"folder › Documents, or use Choose Disc Image.\n\n"
            @"The settings are also in the game: Options, then SELECT. Logs are in Files › Dark Chronicle › "
            @"chronicletwo › logs.",
            DeviceName()];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Getting started"
                                                                   message:text
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end

void IosLauncherRun(const fs::path &data_root) {
    @autoreleasepool {
        CTLauncherController *controller = [[CTLauncherController alloc] initWithRoot:data_root];
        UIWindowScene        *scene = nil;
        for (UIScene *candidate in UIApplication.sharedApplication.connectedScenes) {
            if ([candidate isKindOfClass:UIWindowScene.class]) {
                scene = static_cast<UIWindowScene *>(candidate);
                break;
            }
        }
        UIWindow *window = scene != nil ? [[UIWindow alloc] initWithWindowScene:scene]
                                        : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
        window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
        window.tintColor = Accent();
        // Above the window SDL may show while it starts.
        window.windowLevel = UIWindowLevelNormal + 1;
        window.rootViewController = controller;
        [window makeKeyAndVisible];
        while (!controller.started) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
        }
        window.hidden = YES;
        window.rootViewController = nil;
    }
}
