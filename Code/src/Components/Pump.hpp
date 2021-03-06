#pragma once
#include <KPFoundation.hpp>
#include <Application/Application.hpp>
#include <Application/Constants.hpp>
#include <KPFoundation.hpp>
#include <SD.h>
#include <ArduinoJson.h>

enum class Direction { normal, reverse };

class Pump : public KPComponent {
public:
	using KPComponent::KPComponent;
	const int control1;
	const int control2;

	Pump(const char * name, int control1, int control2)
		: KPComponent(name), control1(control1), control2(control2) {
		pinMode(control1, OUTPUT);
		pinMode(control2, OUTPUT);
		off();
	}

	void on(Direction dir = Direction::normal) {
		delay(20);													// why?
		analogWrite(control1, dir == Direction::normal ? 255 : 0);	// True for normal?
		analogWrite(control2, dir == Direction::normal ? 0 : 255);
	}

	void off() {
		analogWrite(control1, 0);
		analogWrite(control2, 0);
		delay(20);
	}
};