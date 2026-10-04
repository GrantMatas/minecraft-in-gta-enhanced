import dev.rehan.passthrough.AchromaTiming;

public class AchromaTimingTest {
	public static void main(String[] args) {
		long previous = -1;
		for (long real = 0; real <= 330; ++real) {
			long phase = AchromaTiming.phaseAge(real);
			if (phase < previous || phase > previous + 1) throw new AssertionError("Timeline skipped or reversed a phase");
			if (real < 100 && phase >= 70) throw new AssertionError("Impact before five seconds");
			previous = phase;
		}
		if (AchromaTiming.phaseAge(100) != 70 || AchromaTiming.phaseAge(180) != 150 || AchromaTiming.phaseAge(310) != 280)
			throw new AssertionError("Impact/nova/end timing changed");
		if (AchromaTiming.phaseAge(-1) != -1) throw new AssertionError("Future start changed");
		System.out.println("Five-second buildup and subsequent phases passed");
	}
}
