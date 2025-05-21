public class Test7 {
    public static void main(String[] args) {
        System.out.println("Run");
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Done");
    }

    public static int test(int a) {
        int[] arr = {5, 3, 8, 4, 2};
        for (int gap = arr.length / 2; gap > 0; gap /= 2) {
            for (int i = gap; i < arr.length; i++) {
                int temp = arr[i], j;
                for (j = i; j >= gap && arr[j - gap] > temp; j -= gap) {
                    arr[j] = arr[j - gap];
                }
                arr[j] = temp;
            }
        }
        return arr[0];
    }
}

