
import java.util.*;

public class Test13 {
    public static void main(String[] args) {
        System.out.println("Start Test13...");
        for (int i = 0; i < 20000; i++) {
            test(i);
        }
        System.out.println("End Test13.");
    }

    public static void test(int seed) {
        int result = seed;

        // Stage 1: Arithmetic processing
        for (int i = 1; i <= 20; i++) {
            result += (seed ^ i) % 97;
            result ^= (result << 1) + i;
            result -= (result >> 3);
        }

        // Stage 2: Array transformations
        int[] buffer = new int[10];
        for (int i = 0; i < buffer.length; i++) {
            buffer[i] = (result + i * 13) % 1024;
        }

        // Stage 3: Matrix-like computation
        int[][] matrix = new int[5][5];
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 5; j++) {
                matrix[i][j] = (i + 1) * (j + 1) + (result % 7);
                if ((matrix[i][j] + result) % 3 == 0) {
                    result ^= matrix[i][j] * (i + 2);
                }
            }
        }

        // Stage 4: Multi-level conditional logic
        String mode = "BASE";
        if (result % 13 == 0) {
            mode = "ALPHA";
        } else if (result % 11 == 0) {
            mode = "BETA";
        } else if (result % 7 == 0) {
            mode = "GAMMA";
        }

        switch (mode) {
            case "ALPHA":
                result += result % 111;
                break;
            case "BETA":
                result = (result << 2) ^ (result >> 1);
                break;
            case "GAMMA":
                result = result * 3 - 77;
                break;
            default:
                result += 1;
        }

        // Stage 5: String operations
        String text = "Test13_" + seed + "_" + result;
        if (text.hashCode() % 100 == 0) {
            result ^= text.length() * 33;
        }

        // Stage 6: Data structure usage
        Map<String, Integer> map = new HashMap<>();
        for (int i = 0; i < 5; i++) {
            map.put("key" + i, result + i * 17);
        }
        for (String k : map.keySet()) {
            int v = map.get(k);
            if (v % 9 == 0) {
                result ^= v * 7;
            }
        }

        // Final pattern check
        if ((result & 0xFFFF) == 0xBEEF) {
            System.out.println("Found pattern at seed " + seed + ": " + result);
        }
    }
}

