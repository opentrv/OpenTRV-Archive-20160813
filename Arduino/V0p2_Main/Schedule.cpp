/*
The OpenTRV project licenses this file to you
under the Apache Licence, Version 2.0 (the "Licence");
you may not use this file except in compliance
with the Licence. You may obtain a copy of the Licence at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the Licence is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the Licence for the
specific language governing permissions and limitations
under the Licence.

Author(s) / Copyright (s): Damon Hart-Davis 2013--2016
*/

/*
 Schedule support for TRV.
 */

#include <avr/eeprom.h>
#include <util/atomic.h>

#include "Schedule.h"

#include "Control.h"



#if defined(UNIT_TESTS)
// Support for unit tests to force particular apparent schedule state.
// Current override state; 0 (default) means no override.
static _TEST_schedule_override _soUT_override;
// Set the override value (or remove the override).
void _TEST_set_schedule_override(const _TEST_schedule_override override)
  { _soUT_override = override; }
#endif






// All EEPROM activity is made atomic by locking out interrupts where necessary.

// Maximum mins-after-midnight compacted value in one byte.
static const uint8_t MAX_COMPRESSED_MINS_AFTER_MIDNIGHT = ((OTV0P2BASE::MINS_PER_DAY / SIMPLE_SCHEDULE_GRANULARITY_MINS) - 1);


// If LEARN_BUTTON_AVAILABLE then what is the schedule on time?
//#ifdef LEARN_BUTTON_AVAILABLE
// Number of minutes of schedule on time to use.
// Will depend on eco bias.
// TODO: make gradual.
static uint8_t onTime()
  {
#if LEARNED_ON_PERIOD_M == LEARNED_ON_PERIOD_COMFORT_M
  // Simplify the logic where no variation in on time is required.
  return(LEARNED_ON_PERIOD_M); 
#else
  // Variable 'on' time depending on how 'eco' the settings are.

//  // Simple and fast binary choice.
//  return(hasEcoBias() ? LEARNED_ON_PERIOD_M : LEARNED_ON_PERIOD_COMFORT_M);

  // Three-way split based on current WARM target temperature,
  // for a relatively gentle change in behaviour along the valve dial for example.
  const uint8_t wt = getWARMTargetC();
  if(isEcoTemperature(wt)) { return(LEARNED_ON_PERIOD_M); }
  else if(isComfortTemperature(wt)) { return(LEARNED_ON_PERIOD_COMFORT_M); }
  else { return((LEARNED_ON_PERIOD_M + LEARNED_ON_PERIOD_COMFORT_M) / 2); }
#endif
  }
// #endif // LEARN_BUTTON_AVAILABLE

// Pre-warm time before learned/scheduled WARM period,
// based on basic scheduled on time and allowing for some wobble in the timing resolution.
// DHD20151122: even half an hour may not be enough if very cold and heating system not good.
// DHD20160112: with 60m LEARNED_ON_PERIOD_M this should yield ~36m.
const uint8_t PREWARM_MINS = max(30, (SIMPLE_SCHEDULE_GRANULARITY_MINS + (LEARNED_ON_PERIOD_M/2)));
// Setback period before WARM period to help ensure that the WARM target can be reached on time.
// Important for slow-to-heat rooms that have become very cold.
// Similar to or a little longer than PREWARM_MINS
// so that we can safely use this without causing distress, eg waking people up.
// DHD20160112: with 60m LEARNED_ON_PERIOD_M this should yield ~54m for a total run-up of 90m.
const uint8_t PREPREWARM_MINS = (3*(PREWARM_MINS/2));

// Get the simple/primary schedule on time, as minutes after midnight [0,1439]; invalid (eg ~0) if none set.
// Will usually include a pre-warm time before the actual time set.
// Note that unprogrammed EEPROM value will result in invalid time, ie schedule not set.
//   * which  schedule number, counting from 0
uint_least16_t getSimpleScheduleOn(const uint8_t which)
  {
  if(which >= MAX_SIMPLE_SCHEDULES) { return(~0); } // Invalid schedule number.
  uint8_t startMM;
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    { startMM = eeprom_read_byte((uint8_t*)(V0P2BASE_EE_START_SIMPLE_SCHEDULE0_ON + which)); }
  if(startMM > MAX_COMPRESSED_MINS_AFTER_MIDNIGHT) { return(~0); } // No schedule set.
  // Compute start time from stored schedule value.
  uint_least16_t startTime = SIMPLE_SCHEDULE_GRANULARITY_MINS * startMM;
// If LEARN_BUTTON_AVAILABLE then in the absence of anything better SUPPORT_SINGLETON_SCHEDULE should be supported.
#ifdef LEARN_BUTTON_AVAILABLE
  const uint8_t windBackM = PREWARM_MINS; // Wind back start time by about 25% of full interval.
  if(windBackM > startTime) { startTime += OTV0P2BASE::MINS_PER_DAY; } // Allow for wrap-around at midnight.
  startTime -= windBackM;
#endif
  return(startTime);
  }

// Get the simple/primary schedule off time, as minutes after midnight [0,1439]; invalid (eg ~0) if none set.
// This is based on specified start time and some element of the current eco/comfort bias.
//   * which  schedule number, counting from 0
uint_least16_t getSimpleScheduleOff(const uint8_t which)
  {
  const uint_least16_t startMins = getSimpleScheduleOn(which);
  if(startMins == (uint_least16_t)~0) { return(~0); }
  // Compute end from start, allowing for wrap-around at midnight.
  uint_least16_t endTime = startMins + PREWARM_MINS + onTime();
  if(endTime >= OTV0P2BASE::MINS_PER_DAY) { endTime -= OTV0P2BASE::MINS_PER_DAY; } // Allow for wrap-around at midnight.
  return(endTime);
  }

// Set the simple/primary simple on time.
//   * startMinutesSinceMidnightLT  is start/on time in minutes after midnight [0,1439]
//   * which  schedule number, counting from 0
// Invalid parameters will be ignored and false returned,
// else this will return true and isSimpleScheduleSet() will return true after this.
// NOTE: over-use of this routine can prematurely wear out the EEPROM.
bool setSimpleSchedule(const uint_least16_t startMinutesSinceMidnightLT, const uint8_t which)
  {
  if(which >= MAX_SIMPLE_SCHEDULES) { return(false); } // Invalid schedule number.
  if(startMinutesSinceMidnightLT >= OTV0P2BASE::MINS_PER_DAY) { return(false); } // Invalid time.

  // Set the schedule, minimising wear.
  const uint8_t startMM = startMinutesSinceMidnightLT / SIMPLE_SCHEDULE_GRANULARITY_MINS; // Round down...
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    { OTV0P2BASE::eeprom_smart_update_byte((uint8_t*)(V0P2BASE_EE_START_SIMPLE_SCHEDULE0_ON + which), startMM); }
  return(true); // Assume EEPROM programmed OK...
  }

// Clear a simple schedule.
// There will be neither on nor off events from the selected simple schedule once this is called.
//   * which  schedule number, counting from 0
void clearSimpleSchedule(const uint8_t which)
  {
  if(which >= MAX_SIMPLE_SCHEDULES) { return; } // Invalid schedule number.
  // Clear the schedule back to 'unprogrammed' values, minimising wear.
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    { OTV0P2BASE::eeprom_smart_erase_byte((uint8_t*)(V0P2BASE_EE_START_SIMPLE_SCHEDULE0_ON + which)); }
  }

// Returns true if any simple schedule is set, false otherwise.
// This implementation just checks for any valid schedule 'on' time.
// In unit-test override mode is true for soon/now, false for off.
bool isAnySimpleScheduleSet()
  {
#if defined(UNIT_TESTS)
  // Special behaviour for unit tests.
  switch(_soUT_override)
    {
    case _soUT_off: return(false);
    case _soUT_soon: return(true);
    case _soUT_now: return(true);
    }
#endif

  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    {
    for(uint8_t which = 0; which < MAX_SIMPLE_SCHEDULES; ++which)
      {
      if(eeprom_read_byte((uint8_t*)(V0P2BASE_EE_START_SIMPLE_SCHEDULE0_ON + which)) <= MAX_COMPRESSED_MINS_AFTER_MIDNIGHT)
        { return(true); }
      }
    }
  return(false);
  }


// True iff any schedule is currently 'on'/'WARM' even when schedules overlap.
// May be relatively slow/expensive.
// Can be used to suppress all 'off' activity except for the final one.
// Can be used to suppress set-backs during on times.
// In unit-test override mode is true for now, false for soon/off.
bool isAnyScheduleOnWARMNow()
  {
#if defined(UNIT_TESTS)
  // Special behaviour for unit tests.
  switch(_soUT_override)
    {
    case _soUT_off: return(false);
    case _soUT_soon: return(false);
    case _soUT_now: return(true);
    }
#endif

  const uint_least16_t mm = OTV0P2BASE::getMinutesSinceMidnightLT();

  for(uint8_t which = 0; which < MAX_SIMPLE_SCHEDULES; ++which)
    {
    const uint_least16_t s = getSimpleScheduleOn(which);
    if(mm < s) { continue; } // Also deals with case where this schedule is not set at all (s == ~0);
    uint_least16_t e = getSimpleScheduleOff(which);
    if(e < s) { e += OTV0P2BASE::MINS_PER_DAY; } // Cope with schedule wrap around midnight.
    if(mm < e) { return(true); }
    }

  return(false);
  }


// True iff any schedule is due 'on'/'WARM' soon even when schedules overlap.
// May be relatively slow/expensive.
// Can be used to allow room to be brought up to at least a set-back temperature
// if very cold when a WARM period is due soon (to help ensure that WARM target is met on time).
// In unit-test override mode is true for soon, false for now/off.
bool isAnyScheduleOnWARMSoon()
  {
#if defined(UNIT_TESTS)
  // Special behaviour for unit tests.
  switch(_soUT_override)
    {
    case _soUT_off: return(false);
    case _soUT_soon: return(true);
    case _soUT_now: return(false);
    }
#endif

  const uint_least16_t mm0 = OTV0P2BASE::getMinutesSinceMidnightLT() + PREPREWARM_MINS; // Look forward...
  const uint_least16_t mm = (mm0 >= OTV0P2BASE::MINS_PER_DAY) ? (mm0 - OTV0P2BASE::MINS_PER_DAY) : mm0;

  for(uint8_t which = 0; which < MAX_SIMPLE_SCHEDULES; ++which)
    {
    const uint_least16_t s = getSimpleScheduleOn(which);
    if(mm < s) { continue; } // Also deals with case where this schedule is not set at all (s == ~0);
    uint_least16_t e = getSimpleScheduleOff(which);
    if(e < s) { e += OTV0P2BASE::MINS_PER_DAY; } // Cope with schedule wrap around midnight.
    if(mm < e) { return(true); }
    }

  return(false);
  }

