public class Test2 {
    public static void main(String[] args) {
        System.out.println("Run");
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Done");
    }

    public static int test(int a) {
        int[] arr = {5, 3, 8, 4, 2};
        for (int i = 0; i < arr.length - 1; i++) {
            int minIdx = i;
            for (int j = i + 1; j < arr.length; j++)
                if (arr[j] < arr[minIdx])
                    minIdx = j;
            int tmp = arr[i];
            arr[i] = arr[minIdx];
            arr[minIdx] = tmp;
        }
        return arr[0];
    }
}

