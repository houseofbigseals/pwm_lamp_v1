# Драйвер ШИМ  

## Общее описание
Этот проект представляет собой программу для управления 4-канальным ШИМ-драйвером для светодиодной ленты на базе ESP32S3. Максимальный ток через один канал - 4А. Диапазон допустимого напряжения 12-24В. Устройство предназначено для удаленного управления яркостью светильников через Wi-Fi. Программа запускает на чипе ESP32S3 http-сервер с простейшим REST API, позволяющим контролировать скважность ШИМ через HTTP-запросы.       
На плате есть датчик температуры ds18b20 - определяющий температуру платы, чтобы избежать перегрева.
В проекте используется реализация wireguard для esp32 от [trombik](https://github.com/trombik/esp_wireguard) и его форк [droscy](https://github.com/droscy/esp_wireguard)


Важное замечание:   
***Ни при каких обстоятельствах*** не подключайте USB если на прибор подано рабочее напряжение. Это может привести (и скорее всего приведет) к попаданию 12-24В на вход USB. В следующей версии платы это будет исправлено.
Если надо перепрошить устройство, отключите его от сети и потом подключайте к USB.

## Сетевые интерфейсы
Устройство  доступно через следующие сетевые интерфейсы:
- Локальный IPv4 адрес - Выглядит как "192.168.0.123". обычно назначается роутером случайно, поэтому непригоден к использованию. 
- Статический IPv4 адрес в сети Wireguard. Выглядит как "10.10.0.8". По нему может быть доступен удаленно, если клиент тоже подключен к впн-сети.  
- Статический локальный адрес mDNS. Выглядит как "esp32_pwm_lamp_0.local". Вам нужен mDNS сервис типа Avahi чтобы найти в локальной сети девайс по его имени, но это быстрее чем через впн.  

## Подключение к Wi-Fi
Пока что ssid и пароль вайфай сети можно передать девайсу только через перепрошивку прибора:(

## Веб-интерфейс
Устройство поддерживает REST API для управления реле. Ниже приведен список доступных методов:  
 

### GET Получение состояния всех каналов и температуры.
Метод GET на адрес /info  
Пример запроса через curl  (будем использовать адрес устройства 10.10.0.7 для примера)
```
curl -X GET http://10.10.0.7/info
```

Пример ответа:
```
{
	"ch0_pwm":	0,
	"ch1_pwm":	80,
	"ch2_pwm":	0,
	"ch3_pwm":	0,
	"pcb_temp":	26.125,
	"lamp_temp":	0,
	"uptime":	136
}
```
Значение -255 в поле конкретного датчика означает что датчик не установлен или сломан. Uptime измеряется в секундах с последней перезагрузки. 



### Изменение скважности ШИМ на одном из каналов
Метод POST на адрес /pwm. Не забудьте установить тип передаваемых данных в заголовке на 'Content-Type: application/json'  
В качестве аргумента нужно передать JSON слудующего вида:  

```
{"channel":0,"duty":100}
```
Где channel - номер канала реле, может быть от 0 до 3, duty - желаемая величина скважности ШИМ - должна быть от 0 до 100%. 


Пример запроса через curl   (будем использовать адрес устройства 10.10.0.7 для примера)  
```
curl -X POST http://10.10.0.7/pwm -H 'Content-Type: application/json' -d '{"channel":0,"duty":100}'
```

При некорректном запросе выдает следующие ошибки в виде текстовой строки:
1. RESULT: ERR_INCORRECT_JSON - при некорректном формате JSON
2. RESULT: ERR_INVALID_CHANNEL_NUM - при некорректном типе номера канала
3. RESULT: ERR_INVALID_DUTY_VALUE - при некорректном значении новой величины скважности ШИМ


При правильном запросе отдает строку вида:  
```
RESULT: PWM_OPERATION_SUCCESS
; Set channel 0 to 100 % pwm
```

Иногда при выставлении скважности в 0%, устройство может выдавать ответ 
```
RESULT: PWM_OPERATION_SUCCESS
; Set channel 0 to 1 % pwm
```
Это локальная ошибка пересчета, светильники в реальности в этот момент не горят, так что все нормально.

### Удаленная перезагрузка устройства
Метод POST на адрес /reset. Не забудьте установить тип передаваемых данных в заголовке на 'Content-Type: text/html'. Важно передать в качестве аргумента строку "force_reset", только тогда система перезапустится.  

Пример запроса через curl   
```
curl -X POST http://10.10.0.7/reset -H 'Content-Type: text/html' -d 'force_reset'

```

Пример ответа на некорректный запрос:
```
User have sent incorrect reset command, so no rebooting
```

Пример ответа на корректный запрос:
```
User have sent force reset command, so rebooting
```
После этого девайс перезагрузится.  

## Как билдить код
Программа используем измененную схему разделов в памяти, так что надо подключить использование partitions.csv в разделе Flash  и размер прошивки 4 МВ. 
Чтобы заработал порт wireguard, нужно 

