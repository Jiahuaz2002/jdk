public class Test8 {
    public static void main(String[] args) {
        System.out.println("Run");
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Done");
    }

    public static int test(int a) {
        int[] arr = {5, 3, 8, 4, 2};
        int max = 8;
        int[] count = new int[max + 1];
        for (int num : arr) count[num]++;
        int idx = 0;
        for (int i = 0; i <= max; i++)
            while (count[i]-- > 0) arr[idx++] = i;
        return arr[0];
    }
}

