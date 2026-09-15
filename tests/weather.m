// Deterministic checks: no location prompt, network traffic or visible windows.
#import "../src/weather.m"
#include <assert.h>
#include <math.h>

typedef void (^WeatherReply)(NSData*, NSURLResponse*, NSError*);
@interface TestTask : NSObject
@property BOOL cancelled;
@end
@implementation TestTask
- (void)resume {}
- (void)cancel { self.cancelled = YES; }
@end
@interface TestSession : NSObject
@property(copy) WeatherReply reply;
@property(strong) NSURL* requestedURL;
@end
@implementation TestSession
- (NSURLSessionDataTask*)dataTaskWithURL:(NSURL*)url completionHandler:(WeatherReply)reply {
  self.requestedURL = url;
  self.reply = reply;
  return (NSURLSessionDataTask*)[TestTask new];
}
- (void)invalidateAndCancel {}
@end
@interface TestWeather : KnitWeather
@end
@implementation TestWeather
- (void)refresh {} // No real location requests in this test.
@end
static void drain(void) {
  __block BOOL finished = NO;
  dispatch_async(dispatch_get_main_queue(), ^{ finished = YES; });
  while (!finished) [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
}

static NSData* payload(id temperature, id time, NSString* unit) {
  return [NSJSONSerialization dataWithJSONObject:@{
    @"current": @{@"temperature_2m": temperature, @"time": time},
    @"current_units": @{@"temperature_2m": unit, @"time": @"unixtime"}
  } options:0 error:nil];
}
int main(void) {
  @autoreleasepool {
    NSDate* now = [NSDate dateWithTimeIntervalSince1970:1800000000];
    double celsius = 123;
    assert(knit_weather_is_cold(15.5));
    assert(!knit_weather_is_cold((60.0 - 32.0) * 5.0 / 9.0));
    assert(!knit_weather_is_cold(15.6));
    assert(knit_weather_is_cold(-20));
    assert(!knit_weather_is_cold(NAN));
    assert(!knit_weather_is_cold(INFINITY));
    assert(knit_weather_temperature(payload(@15.5, @1800000000, @"°C"), now, &celsius));
    assert(celsius == 15.5);
    assert(!knit_weather_temperature(payload(NSNull.null, @1800000000, @"°C"), now, &celsius));
    assert(!knit_weather_temperature(payload(@YES, @1800000000, @"°C"), now, &celsius));
    assert(!knit_weather_temperature(payload(@"15", @1800000000, @"°C"), now, &celsius));
    assert(!knit_weather_temperature(payload(@50, @1800000000, @"°F"), now, &celsius));
    assert(!knit_weather_temperature(payload(@15, @1799996399, @"°C"), now, &celsius));
    assert(!knit_weather_temperature(payload(@15, @1800000301, @"°C"), now, &celsius));
    assert(!knit_weather_temperature(payload(@999, @1800000000, @"°C"), now, &celsius));
    for (NSString* invalid in @[@"[]", @"null", @"{}", @"{\"current\":[]}", @"invalid"]) {
      assert(!knit_weather_temperature([invalid dataUsingEncoding:NSUTF8StringEncoding], now, &celsius));
    }
    assert(!knit_weather_temperature(nil, now, &celsius));
    KnitWeather* weather = [KnitWeather new];
    assert(!weather.enabled);
    [weather refresh]; // Disabled refresh must not request location.
    assert([weather.status isEqual:@"Weather mode is off"]);
    TestWeather* controlled = [TestWeather new];
    TestSession* session = [TestSession new];
    controlled.session = (NSURLSession*)session;
    CLLocationManager* manager = [CLLocationManager new];
    __block int changes = 0;
    controlled.temperatureChanged = ^(double value) { changes++; assert(value == 10); };
    [controlled setEnabled:YES];
    controlled.locating = YES;
    CLLocation* location = [[CLLocation alloc] initWithCoordinate:CLLocationCoordinate2DMake(51.50741, -0.12781)
      altitude:0 horizontalAccuracy:1000 verticalAccuracy:-1 timestamp:NSDate.date];
    [controlled locationManager:manager didUpdateLocations:@[location]];
    assert([session.requestedURL.query containsString:@"latitude=51.51&longitude=-0.13"]);
    NSHTTPURLResponse* ok = [[NSHTTPURLResponse alloc] initWithURL:session.requestedURL
      statusCode:200 HTTPVersion:nil headerFields:nil];
    NSData* cold = payload(@10, @(NSDate.date.timeIntervalSince1970), @"°C");
    session.reply(cold, ok, nil); drain();
    assert(changes == 1 && !controlled.task);
    controlled.locating = YES;
    [controlled locationManager:manager didUpdateLocations:@[location]];
    session.reply(nil, ok, [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNotConnectedToInternet userInfo:nil]);
    drain();
    assert(changes == 1 && [controlled.status containsString:@"unavailable"]);
    controlled.locating = YES;
    [controlled locationManager:manager didUpdateLocations:@[location]];
    WeatherReply late = session.reply;
    TestTask* pending = (TestTask*)controlled.task;
    [controlled setEnabled:NO];
    assert(pending.cancelled && !controlled.timer && !controlled.task);
    [controlled setEnabled:YES];
    late(cold, ok, nil); drain();
    assert(changes == 1); // Even re-enabling cannot accept the old response.
    [controlled setEnabled:NO];
    puts("weather tests passed: threshold, response validation, rounded location, failure, cancellation");
  }
}
