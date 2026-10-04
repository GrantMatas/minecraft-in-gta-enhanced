package dev.rehan.passthrough;

/** Stretch Achroma's 70-tick buildup to five seconds; preserve every later phase. */
public final class AchromaTiming {
	private AchromaTiming() { }
	public static long phaseAge(long age) {
		return age < 0 ? age : age < 100 ? age * 70 / 100 : age - 30;
	}
}
