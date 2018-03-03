# knx2influx

This tool acts as a simple data gateway from your KNX/IP bus to InfluxDB.

It listens on the KNX/IP multicast address for telegrams and writes the data to InfluxDB.

## Prerequisites

You need a KNX router that sends out KNX/IP multicast packets. Furthermore, you need to configure the filter table of the router to pass the telegrams you want monitored to the IP line.

## Dependencies

This program depends on `libcurl`.

## How to build

`make knx2influx`

## Example config

Minimal config is:

```json
{
  "host": "http://influxhost:8086",
  "database": "dbname",
  "gas": []
}
```

`gas` is a list of objects and describes, which group addresses should be monitored and what type the data has. The syntax is:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9
}
```

`ga` is the group address, is this case 1/2/3.
`series` is the name of the measurement in InfluxDB that it should write the data to.
`dpt` is the Datapoint Type (DPT) of the data. The following DPTs are supported:
* `1`: Mapped to boolean.
* `2`: Mapped to two booleans, one for the data, one for the control.
* `5`: 1 Byte unsigned integer.
* `6`: 1 Byte signed integer.
* `7`: 2 Byte unsigned integer.
* `8`: 2 Byte signed integer.
* `9`: 2 Byte float.
* `12`: 4 Byte unsigned integer.
* `13`: 4 Byte signed integer.
* `14`: 4 Byte float.

Optionally, an additional list of tags can be given:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9,
  "tags": [ "tag1=mytag", "another=tag" ]
}
```

Optionally, a blacklist of senders can be implemented. Telegrams from these senders (e.g., 1.2.3 and 3.4.10) are ignored:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9,
  "ignored_senders": [ "1.0.1", "3.4.10" ]
}
```

The same group address can be specified multiple times, e.g., to write to two series or to write data from one set of senders to one series and from another set of senders to a different series.
