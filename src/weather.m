#import "weather.h"
#import <CoreLocation/CoreLocation.h>
#import <Cocoa/Cocoa.h>
#include <math.h>

BOOL knit_weather_is_cold(double celsius) {
  return isfinite(celsius) && celsius < (60.0 - 32.0) * 5.0 / 9.0;
}

BOOL knit_weather_temperature(NSData* data, NSDate* now, double* celsius) {
  if (!data) return NO;
  id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![json isKindOfClass:NSDictionary.class]) return NO;
  id current = json[@"current"], units = json[@"current_units"];
  if (![current isKindOfClass:NSDictionary.class] || ![units isKindOfClass:NSDictionary.class]) return NO;
  id value = current[@"temperature_2m"], time = current[@"time"];
  if (![value isKindOfClass:NSNumber.class] || ![time isKindOfClass:NSNumber.class]
      || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()
      || CFGetTypeID((__bridge CFTypeRef)time) == CFBooleanGetTypeID()
      || ![units[@"temperature_2m"] isEqual:@"°C"]
      || ![units[@"time"] isEqual:@"unixtime"]) return NO;
  double temperature = [value doubleValue], timestamp = [time doubleValue];
  double age = now.timeIntervalSince1970 - timestamp;
  if (!isfinite(temperature) || temperature < -100 || temperature > 70
      || !isfinite(timestamp) || age < -300 || age > 3600) return NO;
  *celsius = temperature;
  return YES;
}

@interface KnitWeather () <CLLocationManagerDelegate>
@property(nonatomic, readwrite) BOOL enabled;
@property(nonatomic, readwrite, copy) NSString* status;
@property(strong) CLLocationManager* location;
@property(strong) NSURLSession* session;
@property(strong) NSURLSessionDataTask* task;
@property(strong) NSTimer* timer;
@property(strong) NSTimer* locationTimeout;
@property(nonatomic) NSUInteger generation;
@property(nonatomic) BOOL locating;
@end

@implementation KnitWeather
- (instancetype)init {
  if ((self = [super init])) {
    _status = @"Weather mode is off";
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self
      selector:@selector(woke:) name:NSWorkspaceDidWakeNotification object:nil];
  }
  return self;
}
- (void)dealloc {
  [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
  [_timer invalidate];
  [_locationTimeout invalidate];
  [_location stopUpdatingLocation];
  [_session invalidateAndCancel];
}
- (void)woke:(NSNotification*)notification { [self refresh]; }
- (void)setEnabled:(BOOL)enabled {
  if (_enabled == enabled) return;
  _enabled = enabled;
  self.generation++;
  [self.task cancel]; self.task = nil;
  [self.location stopUpdatingLocation]; self.locating = NO;
  [self.locationTimeout invalidate]; self.locationTimeout = nil;
  [self.timer invalidate]; self.timer = nil;
  if (!enabled) { self.status = @"Weather mode is off"; return; }
  __weak KnitWeather* weakSelf = self;
  self.timer = [NSTimer scheduledTimerWithTimeInterval:900 repeats:YES block:^(NSTimer* timer) {
    [weakSelf refresh];
  }];
  self.timer.tolerance = 30;
  [self refresh];
}
- (void)refresh {
  if (!self.enabled || self.locating || self.task) return;
  if (!self.location) {
    self.location = [CLLocationManager new];
    self.location.delegate = self;
    self.location.desiredAccuracy = kCLLocationAccuracyKilometer;
  }
  CLAuthorizationStatus authorization = self.location.authorizationStatus;
  if (authorization == kCLAuthorizationStatusDenied || authorization == kCLAuthorizationStatusRestricted) {
    self.status = @"Allow Location Services in System Settings → Privacy & Security";
    return;
  }
  self.status = @"Finding local weather…";
  self.locating = YES;
  __weak KnitWeather* weakSelf = self;
  self.locationTimeout = [NSTimer scheduledTimerWithTimeInterval:30 repeats:NO block:^(NSTimer* timer) {
    KnitWeather* strongSelf = weakSelf;
    [strongSelf.location stopUpdatingLocation];
    strongSelf.locating = NO;
    strongSelf.status = @"Location unavailable — keeping current sweaters; retrying later";
  }];
  // On macOS this requests permission if it has not been decided yet.
  [self.location requestLocation];
}
- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
  if (!self.enabled) return;
  [self.location stopUpdatingLocation];
  self.locating = NO;
  [self.locationTimeout invalidate]; self.locationTimeout = nil;
  self.generation++;
  [self.task cancel]; self.task = nil;
  [self refresh];
}
- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error {
  if (!self.enabled || !self.locating) return;
  self.locating = NO;
  [self.locationTimeout invalidate]; self.locationTimeout = nil;
  self.status = error.code == kCLErrorDenied
    ? @"Allow Location Services in System Settings → Privacy & Security"
    : @"Location unavailable — keeping current sweaters; retrying later";
}
- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
  if (!self.enabled || !self.locating) return;
  CLLocation* location = locations.lastObject;
  if (!location || location.horizontalAccuracy < 0
      || fabs(location.timestamp.timeIntervalSinceNow) > 300
      || !CLLocationCoordinate2DIsValid(location.coordinate)) return;
  self.locating = NO;
  [self.locationTimeout invalidate]; self.locationTimeout = nil;
  // Only city-scale coordinates leave the device. Do not persist location.
  double latitude = round(location.coordinate.latitude * 100) / 100;
  double longitude = round(location.coordinate.longitude * 100) / 100;
  NSString* url = [NSString stringWithFormat:
    @"https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m&temperature_unit=celsius&timeformat=unixtime&forecast_days=1",
    latitude, longitude];
  if (!self.session) {
    NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    configuration.timeoutIntervalForRequest = 20;
    configuration.timeoutIntervalForResource = 30;
    configuration.URLCache = nil;
    self.session = [NSURLSession sessionWithConfiguration:configuration];
  }
  NSUInteger generation = self.generation;
  self.status = @"Checking local temperature…";
  __weak KnitWeather* weakSelf = self;
  self.task = [self.session dataTaskWithURL:[NSURL URLWithString:url]
    completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        KnitWeather* strongSelf = weakSelf;
        if (!strongSelf || !strongSelf.enabled || strongSelf.generation != generation) return;
        strongSelf.task = nil;
        double celsius;
        if (error || ![response isKindOfClass:NSHTTPURLResponse.class]
            || ((NSHTTPURLResponse*)response).statusCode != 200
            || !knit_weather_temperature(data, NSDate.date, &celsius)) {
          strongSelf.status = @"Weather unavailable — keeping current sweaters; retrying later";
          return;
        }
        strongSelf.status = [NSString stringWithFormat:@"%.1f°F / %.1f°C — checked %@",
          celsius * 9.0 / 5.0 + 32.0, celsius,
          [NSDateFormatter localizedStringFromDate:NSDate.date dateStyle:NSDateFormatterNoStyle timeStyle:NSDateFormatterShortStyle]];
        if (strongSelf.temperatureChanged) strongSelf.temperatureChanged(celsius);
      });
    }];
  [self.task resume];
}
@end
