using System;

using System;



class Program
{
    private function bool on_send_recv_delegate(uint8* data, ref int length);

    struct plugin_header
    {
        public on_send_recv_delegate on_recv;
        public on_send_recv_delegate on_send;
    }

    private static bool on_recv(uint8* data, ref int length)
    {
         //Console.WriteLine(scope String().AppendF("RECV\tID: {}", data[0]));
         return true;
    }

    private static bool on_send(uint8* data, ref int length)
    {
         //Console.WriteLine(scope String().AppendF("SEND\tID: {}", data[0]));
         return true;
    }


    [CLink]
    [Export]
    public static void Install(void* ptr)
    {
        plugin_header* plugin = (plugin_header*) ptr;

        plugin.on_recv = => on_recv;
        plugin.on_send = => on_send;
    
        //Console.WriteLine("INSTALLED!!!");
    }
}

