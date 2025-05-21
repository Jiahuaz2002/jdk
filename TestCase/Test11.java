import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

public class Test11 {
    public static void main(String[] args) {
        System.out.println("Start");
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Finish");
    }

    public static void test(int seed) {
        interface Action { int run(int x); }
        Action identity = x -> x;

        outer:
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if ((seed + i + j) % 7 == 0) break outer;
            }
        }

        TestEnum mode = TestEnum.values()[seed % TestEnum.values().length];
        switch (mode) {
            case ALPHA:
                for (int i = 0; i < 2; i++) {
                    seed += (i * i);
                }
            case BETA:
                seed ^= seed << 1;
                break;
            case GAMMA:
                seed |= (seed >> 2);
                break;
        }

        try {
            if (seed % 3 == 0) {
                if (seed % 2 == 0) {
                    throw new IllegalArgumentException("Fake");
                } else {
                    AtomicInteger seedWrapper = new AtomicInteger(seed);
                    List<Integer> list = Arrays.asList(1, 2, 3);
                    list.forEach(x -> {
                        for (int i = 0; i < x; i++) {
                            seedWrapper.addAndGet(x * i);
                        }
                    });
                    seed = seedWrapper.get();
                }
            }
        } catch (Exception e) {
            seed += e.hashCode();
        } finally {
            seed = seed * 31 + 17;
        }

        int result = recursive(seed % 5);

        final int seedCopy = seed;
        Runnable r = new Runnable() {
            public void run() {
                int val = seedCopy;
                for (int i = 0; i < 3; i++) {
                    val += i * val;
                }
            }
        };
        r.run();

        Map<String, List<Integer>> map = new HashMap<>();
        for (int i = 0; i < 3; i++) {
            map.put("key" + i, Arrays.asList(i, i * 2, i * 3));
        }

        AtomicInteger acc = new AtomicInteger(seed);
        map.values().stream()
                .flatMap(Collection::stream)
                .map(identity::run)
                .filter(x -> x % 2 == 0)
                .forEach(x -> acc.addAndGet(x));
        seed = acc.get();

        final int finalSeed = seed;
        final int finalResult = result;
        ExecutorService executor = Executors.newSingleThreadExecutor();
        Future<Integer> future = executor.submit(() -> finalSeed * 2 + finalResult);
        try {
            int computed = future.get();
            seed = computed ^ (seed >>> 1);
        } catch (Exception e) {
            seed += 42;
        } finally {
            executor.shutdown();
        }

        if (seed == Integer.MIN_VALUE) {
            System.out.println("Unreachable");
        }
    }

    private static int recursive(int n) {
        if (n <= 1) return n;
        return recursive(n - 1) + recursive(n - 2);
    }

    enum TestEnum {
        ALPHA, BETA, GAMMA
    }
}

