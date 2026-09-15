#import <Foundation/Foundation.h>

// All control methods and callbacks run on the main thread.
@interface KnitWeather : NSObject
@property(nonatomic, readonly) BOOL enabled;
@property(nonatomic, readonly, copy) NSString* status;
@property(nonatomic, copy) void (^temperatureChanged)(double celsius);
- (void)setEnabled:(BOOL)enabled;
- (void)refresh;
@end

BOOL knit_weather_temperature(NSData* data, NSDate* now, double* celsius);
BOOL knit_weather_is_cold(double celsius);
