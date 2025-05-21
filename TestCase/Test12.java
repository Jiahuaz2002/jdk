
import java.util.*;

public class Test12 {
    public static void main(String[] args) {
        System.out.println("Start Test13...");
        for (int i = 0; i < 20000; i++) {
            test(i);
        }
        System.out.println("End Test13.");
    }

    public static void test(int seed) {
        int result = seed;

        // Simple mathematical operations
        for (int i = 1; i <= 10; i++) {
            result += (seed ^ i) % 97;
            result ^= (result << 1) + i;
        }

        // Basic array operations
        int[] array = new int[5];
        for (int i = 0; i < array.length; i++) {
            array[i] = (result + i * 3) % 255;
        }

        // Nested loop and condition
        for (int i = 0; i < array.length; i++) {
            for (int j = i; j < array.length; j++) {
                if ((array[i] + array[j]) % 7 == 0) {
                    result ^= array[i] * array[j];
                }
            }
        }

        // Emulate a small state machine
        String state = "INIT";
        if (result % 5 == 0) {
            state = "MOD5";
        } else if (result % 3 == 0) {
            state = "MOD3";
        } else {
            state = "OTHER";
        }

        // Final transformation
        switch (state) {
            case "MOD5":
                result += 100;
                break;
            case "MOD3":
                result -= 50;
                break;
            case "OTHER":
                result *= 2;
                break;
        }

        // Print only if interesting
        if ((result & 0xFF) == 0xAA) {
            System.out.println("Pattern matched at seed " + seed + ": " + result);
        }
    }
}

