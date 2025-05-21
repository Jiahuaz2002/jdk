public class Test4 {
    public static void main(String[] args) {
        System.out.println("Run");
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Done");
    }

    public static int test(int a) {
        int[] arr = {5, 3, 8, 4, 2};
        mergeSort(arr, 0, arr.length - 1);
        return arr[0];
    }

    private static void mergeSort(int[] arr, int left, int right) {
        if (left < right) {
            int m = (left + right) / 2;
            mergeSort(arr, left, m);
            mergeSort(arr, m + 1, right);
            merge(arr, left, m, right);
        }
    }

    private static void merge(int[] arr, int l, int m, int r) {
        int[] tmp = new int[r - l + 1];
        int i = l, j = m + 1, k = 0;
        while (i <= m && j <= r) {
            if (arr[i] <= arr[j]) tmp[k++] = arr[i++];
            else tmp[k++] = arr[j++];
        }
        while (i <= m) tmp[k++] = arr[i++];
        while (j <= r) tmp[k++] = arr[j++];
        for (int x = 0; x < tmp.length; x++) arr[l + x] = tmp[x];
    }
}

