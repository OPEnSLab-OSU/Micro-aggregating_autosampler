#include <Procedures/SampleStates.hpp>
#include <Application/Application.hpp>
//#include <FileIO/SerialSD.hpp>
#include <time.h>
#include <sstream>
#include <String>

bool pumpOff = 1;
bool flushVOff = 1;
bool sampleVOff = 1;
bool pressureEnded = 0;
unsigned long sample_start_time;
unsigned long sample_end_time;
short load_count = 0;
float prior_load = 0;


//SerialSD SSD;
CSVWriter csvw{"data.csv"};

void writeLatch(bool controlPin, ShiftRegister & shift) {
	shift.setPin(controlPin, HIGH);
	shift.write();
	delay(80);
	shift.setPin(controlPin, LOW);
	shift.write();
}

// Idle
void SampleStateIdle::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	if (app.sm.current_cycle < app.sm.last_cycle)
		setTimeCondition(time, [&]() { sm.transitionTo(SampleStateNames::ONRAMP); });
	else
		sm.transitionTo(SampleStateNames::FINISHED);
}

// Stop
void SampleStateStop::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	app.pump.off();
	sample_end_time = millis();
	print("sample_end_time ms;;;");
	println(sample_end_time);
	const auto timenow = now();
	std::stringstream ss;
	ss << timenow;
	std::string time_string = ss.str();
	char cycle_string[50];
	sprintf(cycle_string, "%u", (int)app.sm.current_cycle);
	std::string strings[4] = {"Sample ", cycle_string, " Stop Time", time_string};
	csvw.writeStrings(strings, 4);
	pumpOff = 1;
	println("Pump off");

	app.shift.writeAllRegistersLow();
	app.shift.writeLatchOut();
	flushVOff = 1;
	sampleVOff = 1;

	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStateOnramp::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);

	if (flushVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::FLUSH_VALVE, HIGH);  // write in skinny
		app.shift.write();								   // write shifts wide*/
		flushVOff = 0;
		println("Flush valve turning on");
		sampleVOff = 1;
	}

	setTimeCondition(time, [&]() { sm.next(); });
}

// Flush
void SampleStateFlush::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);

	if (flushVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::FLUSH_VALVE, HIGH);  // write in skinny
		app.shift.write();								   // write shifts wide*/
		flushVOff = 0;
		println("Flush valve turning on");
		sampleVOff = 1;
	}

	if (pumpOff){
		app.pump.on();
		pumpOff = 0;
		println("Pump on");
	}

	setTimeCondition(time, [&]() { sm.next(); });
}

// Sample
void SampleStateSample::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	//SSD.print("Input total sampling mass in g;;;");
	//SSD.println(mass);
	//SSD.print("Input total sampling time in ms;;;");
	//SSD.println(time);

	current_tare = app.sm.getState<SampleStateLoadBuffer>(SampleStateNames::LOAD_BUFFER).current_tare;

	if (sampleVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::WATER_VALVE, HIGH);
		app.shift.write();
		sampleVOff = 0;
		println("Sample valve turning on");
		flushVOff = 1;
	}

	const auto timenow = now();
	std::stringstream ss;
	ss << timenow;
	std::string time_string = ss.str();
	char cycle_string[50];
	sprintf(cycle_string, "%u", (int)app.sm.current_cycle);
	std::string strings[4] = {"Sample ", cycle_string, " Start Time", time_string};
	csvw.writeStrings(strings, 4);
	sample_start_time = millis();
	print("sample_start_time ms ;;;");
	println(sample_start_time);

	if (pumpOff){
		app.pump.on();
		pumpOff = 0;
		println("Pump on");
	}

	auto const condition = [&]() {
		bool load = 0;
		bool load_rate = 0;
		// check load, exit if matching mass
		new_load = app.load_cell.getLoad(1);
		new_time = millis();
		print("New load reading;");
		println(new_load);
		print("New time;;;");
		println(new_time);
		//print("Current_tare in sample state;;;; ");
		//println(current_tare);
		//print("mass var in sample state;;;; ");
		//println(mass);
		load = new_load - current_tare >= (mass - 0.05*mass);
		if (load){
			//SSD.println("Sample state ended due to: load ");
			std::string temp[1] = {"Sample state ended due to: load "};
			csvw.writeStrings(temp, 1);
			println("Sample state ended due to: load ");
			pressureEnded = 0;
			return load;
		}
			//if not exiting due to load, check time and exit if over time
		else{
			bool t_max = timeSinceLastTransition() >= secsToMillis(time);
			bool t_adj = timeSinceLastTransition() >= time_adj_ms;
			if (t_max || t_adj){
				std::string temp[1] = {"Sample state ended due to: time"};
				csvw.writeStrings(temp, 1);
				println("Sample state ended due to: time");
				pressureEnded = 0;
				return t_max || t_adj;
			}
			//if not exiting due to load and time, check pressure
			else{
				bool pressure = !app.pressure_sensor.isWithinPressure();
				if (pressure){
					std::string temp[1] = {"Sample state ended due to: pressure"};
					csvw.writeStrings(temp, 1);
					println("Sample state ended due to: pressure");
					pressureEnded = 1;
					return pressure;
				}
				//if not exiting due to load, time, and pressure, check pumping rate
				else{
					if (load_count > 0){
						//print("prior_load in sample states;;;;");
						//println(prior_load);
						//print("prior_time in sample states;;;;");
						//println(prior_time);
						//new_rate = (new_load - prior_load) / ((new_time - prior_time));
						//print("new_load - prior_load;;;");
						//println(new_load - prior_load);
						//print("(new_time - prior_time);;;");
						//println((new_time - prior_time));
						//print("New rate grams/ms;;;");
						//println(new_rate);
						//print("average rate: new_load - current_tare / new_time - sample_start_time;;;");
						accum_load = new_load - current_tare;
						//print("Accumulated load (g); ");
						//println(accum_load);
						accum_time = new_time - sample_start_time;
						//print("Accumulated time (ms);;; ");
						//println(accum_time);
						avg_rate = 1000*(accum_load/accum_time);
						print("Average rate in g/s;;;; ");
						println(avg_rate,4);
						//println(avg_rate,3);
						//check to see if sampling time is appropriate
						print("Coded time remaining in millis;;;");
						code_time_est = time_adj_ms - timeSinceLastTransition();
						println(code_time_est);
						// update time if more than 10% off and new load - tare > 1
						if (load_count > 5){
							if (new_load - current_tare > 1){
								weight_remaining = mass - (new_load - current_tare);
								print("Weight remaining (mass - (new_load - current_tare));");
								println(weight_remaining);
								// calculate new time based upon average rate
								new_time_est = weight_remaining/((new_load - current_tare)/(new_time - sample_start_time));
								print("Estimated time remaining in ms: weight_remaining/average rate;;;");
								println(new_time_est);
								if (abs((code_time_est - new_time_est)/code_time_est) > 0.1){
									time_adj_ms = new_time_est + timeSinceLastTransition();
									print("Code time outside 10 percent of estimated time. Updated sampling time in millis;;;");
									println(time_adj_ms);
								}
								// check in to see if pumping rate is really slow (half expected rate) meaning getting a lot of air
								load_rate = abs(avg_rate) < 0.004;
								/*if (load_rate){
									//SSD.println("Sample state ended due to: load ");
									std::string temp[1] = {"Sample state ended due to: low load rate"};
									csvw.writeStrings(temp, 1);
									println("Sample state ended due to: low load rate");
									pressureEnded = 0;
									return load_rate;
								}*/
							}
						}
					}
					prior_load = new_load;
					prior_time = new_time;
					prior_rate = new_rate;
					prior_time_est = new_time_est;
					load_count += 1;
					return load || t_max || t_adj || pressure;// || load_rate;
					}
				}
			}
	};
	setCondition(condition, [&]() { sm.next(); });
}

// Sample leave
void SampleStateSample::leave(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	load_count = 0;
	prior_load = 0;
	app.sm.current_cycle += 1;
}

// Finished
void SampleStateFinished::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	app.led.setFinished();
	app.sm.reset();
}

// Setup
void SampleStateSetup::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	app.led.setRun();
	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStateBetweenPump::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	app.pump.off();
	pumpOff = 1;
	println("Pump off");
	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStateBetweenValve::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	
	if (sampleVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::WATER_VALVE, HIGH);
		app.shift.write();
		sampleVOff = 0;
		println("Sample valve turning on");
		flushVOff = 1;
	}
	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStateFillTubeOnramp::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);

	if (flushVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::FLUSH_VALVE, HIGH);  // write in skinny
		app.shift.write();								   // write shifts wide*/
		flushVOff = 0;
		println("Flush valve turning on");
		sampleVOff = 1;
	}
	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStateFillTube::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);

	if (flushVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::FLUSH_VALVE, HIGH);  // write in skinny
		app.shift.write();								   // write shifts wide*/
		flushVOff = 0;
		println("Flush valve turning on");
		sampleVOff = 1;
	}
	if (pumpOff){
		app.pump.on();
		pumpOff = 0;
		println("Pump on");
	}

	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStatePressureTare::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	if (flushVOff){
		app.shift.setAllRegistersLow();
		app.shift.setPin(TPICDevices::FLUSH_VALVE, HIGH);  // write in skinny
		app.shift.write();								   // write shifts wide*/
		flushVOff = 0;
		println("Flush valve turning on");
		sampleVOff = 1;		
	}
	if (pumpOff){
		app.pump.on();
		pumpOff = 0;
		println("Pump on");
	}

	sum	  = 0;
	count = 0;

	setTimeCondition(time, [&]() { sm.next(); });
}

void SampleStatePressureTare::update(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	sum += app.pressure_sensor.getPressure();
	++count;
}

void SampleStatePressureTare::leave(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
#ifndef DISABLE_PRESSURE_TARE
	int avg = sum / count;
	char press_string[50];
	sprintf(press_string, "%d.%02u", (int)avg, (int)((avg - (int)avg) * 100));
	std::string strings[2] = {"Normal pressure set to value", press_string};
	csvw.writeStrings(strings, 2);
	print("Normal pressure set to value: ");
	println(avg);

	app.pressure_sensor.max_pressure = avg + range_size;
	app.pressure_sensor.min_pressure = avg - range_size;
	print("Max pressure: ");
	println(app.pressure_sensor.max_pressure);
	print("Min pressure: ");
	println(app.pressure_sensor.min_pressure);
#endif
#ifdef DISABLE_PRESSURE_TARE
	SSD.println("Pressure tare state is disabled.");
	SSD.println("If this is a mistake, please remove DISABLE_PRESSURE_TARE from the buildflags.");
	SSD.print("Max pressure (set manually): ");
	SSD.println(app.pressure_sensor.max_pressure);
	SSD.print("Min pressure (set manually): ");
	SSD.println(app.pressure_sensor.min_pressure);
#endif
}

void SampleStateLogBuffer::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	print("Load at end of cycle ");//+ app.sm.current_cycle + ": " + (double)app.load_cell.getLoad());
	print(app.sm.current_cycle);
	print("; ");
	final_load = app.load_cell.getLoad(25);
	println(final_load,3);
	//print to SD
	char cycle_string[50];
	sprintf(cycle_string, "%u", (int)app.sm.current_cycle);
	char load_string[50];
	sprintf(load_string, "%d.%02u", (int)final_load, (int)((final_load - (int)final_load) * 100));
	std::string strings[3] = {"Load at end of cycle", cycle_string, load_string};
	csvw.writeStrings(strings, 3);

	//evaluate load and sampling time
	current_tare = app.sm.getState<SampleStateLoadBuffer>(SampleStateNames::LOAD_BUFFER).current_tare;

	//print("current_tare in log buffer;;;;");
	//println(current_tare);
	sampledLoad = final_load - current_tare;
	print("sampledLoad: final_load - current_tare;");
	println(sampledLoad);
	//print("sample_start_time ms in log buffer;;;");
	//println(sample_start_time);
	//print("sample_end_time ms in log buffer;;;");
	//println(sample_end_time);
	sampledTime = (sample_end_time - sample_start_time);
	print("sampledTime period in ms;;");
	println(sampledTime);
	//calculate average pumping rate
	average_pump_rate = (sampledLoad / sampledTime)*1000;
	print("Average pumping rate grams/sec: 1000*sampledLoad/sampledTime;;;");
	println(average_pump_rate,4);
	//change time opposite sign of load diff (increase for negative, decrease for positive)
	mass = app.sm.getState<SampleStateSample>(SampleStateNames::SAMPLE).mass;
	print("load_diff: mass - sampledLoad;;;");
	load_diff = mass - sampledLoad;
	println(load_diff);

	//update time if sample didn't end due to pressure
	if (!pressureEnded){	
		// change sampling time if load was +- 5% off from set weight
		if (abs(mass - sampledLoad)/mass > 0.05){
			println("Sample mass outside of 5 percent tolerance");
			sampledTime += (load_diff)/average_pump_rate;
			print("new sampling time period in ms: load diff/avg rate;;");
		}
		else {
			print("Sampling time period set to match last sample time;;");
		}
		println(sampledTime);
		//set new sample time
		app.sm.getState<SampleStateSample>(SampleStateNames::SAMPLE).time_adj_ms = sampledTime;
	}
	sm.next();
}

void SampleStateLoadBuffer::enter(KPStateMachine & sm) {
	Application & app = *static_cast<Application *>(sm.controller);
	print("Temp: ");
	tempC = app.pressure_sensor.getTemp();
	println(tempC);
	char cycle_string[50];
	sprintf(cycle_string, "%u", (int)app.sm.current_cycle);
	char tempC_string[50];
	sprintf(tempC_string, "%d.%02u", (int)tempC, (int)((tempC - (int)tempC) * 100));
	std::string strings[3] = {"Temperature for sample", cycle_string, tempC_string};
	csvw.writeStrings(strings, 3);
	//SSD.println("Get load");
	//println(app.load_cell.getLoad(1));
	print("Tare load; ");
	current_tare = app.load_cell.reTare(25);
	println(current_tare);
	char tare_string[50];
	sprintf(tare_string, "%d.%02u", (int)current_tare, (int)((current_tare - (int)current_tare) * 100));
	std::string tare_strings[3] = {"Tare load for sample", cycle_string, tare_string};
	csvw.writeStrings(tare_strings, 3);
	sm.next();
}