package com.tencent.mars.webserver;

import com.google.protobuf.InvalidProtocolBufferException;
import com.tencent.mars.sample.proto.Main;

import junit.framework.Assert;

import org.junit.Test;


/**
 *
 * Created by kirozhao on 16/2/2.
 */
public class EchoCgiTest {

    @Test
    public void testHello() throws Exception {

        final Main.HelloRequest request = Main.HelloRequest.newBuilder()
                .setUser("dkyang")
                .setText("hello")
                .build();

        final Main.HelloResponse response = Main.HelloResponse.parseFrom(
                WebAppTestFramework.post("mars/hello", request.toByteArray())
        );

        System.out.println(response.getErrmsg());

        Assert.assertTrue(response.getRetcode() == 0);
    }

    @Test
    public void testHelloFakeResponse() throws Exception {
        try {
            final Main.HelloResponse response = Main.HelloResponse.parseFrom(
                    WebAppTestFramework.post("mars/hello", new byte[100])
            );

        } catch (InvalidProtocolBufferException e) {
            e.printStackTrace();

            return;
        }

        Assert.assertTrue(false);
    }

}
