/*
 * temp_sensor.h - die (junction) temperature in Celsius via ADC3 IN18,
 * factory-calibrated. Lazy-inits the ADC on first call (~blocking, fast).
 */
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

int temp_read_c(void);

#endif /* TEMP_SENSOR_H */
