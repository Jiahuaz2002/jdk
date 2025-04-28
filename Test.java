public class Test {
    public static void main(String[] args) {
        // Especially with a debug build, the JVM startup can take a while,
        // so it can take a while until our code is executed.
        System.out.println("Run");

        // Repeatedly call the test method, so that it can become hot and
        // get JIT compiled.
        for (int i = 0; i < 10_000; i++) {
            test(i);
        }
        System.out.println("Done");
    }
    
    // The test method we will focus on.
    public static int test(int a) {
        int i;
        for(i=0;i<10;++i)
        {
                if(i%2==0)
                        ++a;
                else 
                    a+=2;
        }
        return a;
    }
}
