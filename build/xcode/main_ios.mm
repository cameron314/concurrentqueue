#import <UIKit/UIKit.h>

// runAllTests should be defined by the main unit test compilation
// unit (unittests.cpp)
bool runAllTests();

// Define a minimal UIApplicationDelegate.
@interface AppDelegate : NSObject <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation AppDelegate

- (BOOL)application:(__unused UIApplication *)application
    didFinishLaunchingWithOptions:(__unused NSDictionary *)launchOptions {
  // Xcode / iOS will complain unless a window and a root view
  // controller is setup at application launch.
  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  self.window.rootViewController = [[UIViewController alloc] initWithNibName:nil bundle:nil];
  [self.window makeKeyAndVisible];
  dispatch_async(dispatch_get_main_queue(), ^{
    [self run];
  });
  return YES;
}

-(void) run {
  NSLog(@"Starting unittests run...");
  const bool result = runAllTests();
  NSLog(@"%@", result ? @"All tests passed" : @"Test(s) failed!");
}

@end

int main(int argc, char *argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
  }
}
