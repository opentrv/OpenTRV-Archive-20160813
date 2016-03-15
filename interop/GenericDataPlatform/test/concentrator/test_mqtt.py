import unittest

import datetime

import opentrv.data
import opentrv.concentrator.mqtt

class MockMqttMessage(object):
    def __init__(self, topic, qos, payload):
        self.topic = topic
        self.qos = qos
        self.payload = payload.encode("utf-8")

class MockMqttSink(object):
    def __init__(self):
        self.records = None

    def on_message(self, records):
        self.records = records

class TestMqttSubscriber(unittest.TestCase):
    def test_parse(self):
        payload = """{
            "ts": "2016-01-01T05:10:15Z",
            "body": {
                "T|C": 14.5,
                "O": 2
            }
        }"""
        m = opentrv.concentrator.mqtt.Subscriber(
            None, server="", port=0, client="", topic="")
        r = m.parse("topic", payload)
        expected_keys = ["T", "O"]
        expected = {
            "T": opentrv.data.Record(
                "T",
                datetime.datetime(2016, 1, 1, 5, 10, 15),
                14.5, "C", "topic"
            ),
            "O": opentrv.data.Record(
                "O",
                datetime.datetime(2016, 1, 1, 5, 10, 15),
                2, None, "topic"
            )
        }
        self.assertEqual(2, len(r))
        for i in range(0, len(r)):
            n = r[i].name
            expected_keys.remove(n)
            self.assertEqual(expected[n].name, r[i].name)
            self.assertEqual(expected[n].timestamp, r[i].timestamp)
            self.assertEqual(expected[n].value, r[i].value)
            self.assertEqual(expected[n].unit, r[i].unit)
            self.assertEqual(expected[n].topic, r[i].topic.path())

    def test_on_message(self):
        t = "topic"
        p = "".join(["{\"ts\":\"2016-03-12T20:38:00Z\",",
            "\"body\":{\"T|C\":20}}"])
        m = MockMqttMessage(t, 0, p)
        snk = MockMqttSink()
        sub = opentrv.concentrator.mqtt.Subscriber(
            snk, server="", port=0, client="", topic="")
        sub.on_message(None, None, m)
        self.assertIsNotNone(snk.records)
        self.assertEqual(1, len(snk.records))
        self.assertEqual("topic", snk.records[0].topic.path())
        self.assertEqual("T", snk.records[0].name)
        self.assertEqual("C", snk.records[0].unit)

if __name__ == '__main__':
    unittest.main()