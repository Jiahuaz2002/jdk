
import java.util.*;
import java.util.concurrent.atomic.*;
import java.util.function.*;
import java.util.stream.*;

public class Test14 {
    public static void main(String[] args) {
        System.out.println("Start Test13...");
        for (int i = 0; i < 20000; i++) {
            test(i);
        }
        System.out.println("End Test13.");
    }

    public static void test(int seed) {
        AtomicInteger result = new AtomicInteger(seed);

        // Stage 1: Arithmetic processing
        IntStream.range(1, 30).forEach(i -> {
            result.addAndGet((seed ^ i) % 101);
            result.set(result.get() ^ ((result.get() << 1) + i));
            result.set(result.get() - (result.get() >> 2));
        });

        // Stage 2: Buffer initialization
        List<Integer> buffer = IntStream.range(0, 20)
            .map(i -> (result.get() + i * 11) % 2048)
            .boxed()
            .collect(Collectors.toList());

        // Stage 3: Matrix-like computation with stream
        List<List<Integer>> matrix = IntStream.range(0, 5)
            .mapToObj(i -> IntStream.range(0, 5)
                .map(j -> (i + 1) * (j + 1) + (result.get() % 9))
                .boxed()
                .collect(Collectors.toList()))
            .collect(Collectors.toList());

        matrix.forEach(row -> row.forEach(val -> {
            if ((val + result.get()) % 5 == 0) {
                result.set(result.get() ^ (val * 3));
            }
        }));

        // Stage 4: Conditional logic
        String mode = "INIT";
        if (result.get() % 17 == 0) {
            mode = "OMEGA";
        } else if (result.get() % 13 == 0) {
            mode = "DELTA";
        } else if (result.get() % 7 == 0) {
            mode = "THETA";
        }

        switch (mode) {
            case "OMEGA":
                result.set(result.get() + result.get() % 123);
                break;
            case "DELTA":
                result.set((result.get() << 3) ^ (result.get() >> 2));
                break;
            case "THETA":
                result.set(result.get() * 5 - 89);
                break;
            default:
                result.incrementAndGet();
        }

        // Stage 5: Stream string transformation
        String combined = IntStream.range(0, 10)
            .mapToObj(i -> "X" + ((char)('A' + i)) + seed)
            .collect(Collectors.joining("-"));

        if (combined.hashCode() % 256 == 0) {
            result.set(result.get() ^ combined.length() * 17);
        }

        // Stage 6: Stream + map + reduction
        Map<String, Integer> map = IntStream.range(0, 10)
            .boxed()
            .collect(Collectors.toMap(i -> "k" + i, i -> result.get() + i * 29));

        int reduced = map.values().stream()
            .filter(v -> v % 4 != 0)
            .mapToInt(v -> v ^ 5)
            .reduce(0, Integer::sum);

        result.addAndGet(reduced);

        // Stage 7: Final pattern check
        if ((result.get() & 0xFFFF) == 0xDEAD) {
            System.out.println("Signature match at seed " + seed + ": " + result.get());
        }
    }
}

