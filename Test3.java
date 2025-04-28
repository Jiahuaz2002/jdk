public class Test3{
    public static void main(String[] args) {
        System.out.println("Run");

        int[] arr = {5, 3, 8, 4, 2};
        for(int i=0;i<10_000;++i)
            test(arr);

        System.out.println("Sorted array:");
        for (int num : arr) {
            System.out.print(num + " ");
        }

        System.out.println("\nDone");
    }

    // Simple bubble sort implementation
    public static void test(int[] array) {
        int n = array.length;
        boolean swapped;

        for (int i = 0; i < n - 1; i++) {
            swapped = false;

            for (int j = 0; j < n - i - 1; j++) {
                if (array[j] > array[j + 1]) {
                    // Swap elements
                    int temp = array[j];
                    array[j] = array[j + 1];
                    array[j + 1] = temp;

                    swapped = true;
                }
            }

            // If no swaps happened, the array is already sorted
            if (!swapped) break;
        }
    }
}

