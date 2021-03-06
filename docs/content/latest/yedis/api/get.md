---
title: GET
linkTitle: GET
description: GET
menu:
  latest:
    parent: api-yedis
    weight: 2070
aliases:
  - /latest/api/redis/get
  - /latest/api/yedis/get
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`GET key`</b><br>
This command fetches the value that is associated with the given `key`.

<li>If the `key` does not exist, null is returned.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## Return Value
Returns string value of the given `key`.

## Examples

```sh
$ GET yugakey
```

```
(null)
```

```sh
$ SET yugakey "YugaByte"
```

```
"OK"
```

```sh
$ GET yugakey
```

```
"YugaByte"
```


## See Also
[`append`](../append/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
