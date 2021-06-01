# hdc组件<a name="ZH-CN_TOPIC_0000001149090043"></a>

-   [简介](#section662115419449)
-   [架构](#section15908143623714)
-   [目录](#section161941989596)
    -   [使用说明](#section129654513264)

-   [相关仓](#section1371113476307)

## 简介<a name="section662115419449"></a>

hdc（Harmony Device Connector） 是OpenHarmony为开发人员提供的用于调试的命令行工具，开发机器上使用设备连接器hdc client/server命令行工具，该工具需支持部署在Windows/Linux/Mac等系统上与鸿蒙设备（或模拟器）进行连接调试通信。PC端hdc工具需要针对以上开发机操作系统平台分别发布相应的版本，设备端hdc daemon需跟随设备镜像发布包括对模拟器进行支持。下文将介绍hdc的常用命令及使用举例。

## 架构<a name="section15908143623714"></a>

hdc主要有三部分组成：

1. hdc client部分：运行于开发机上的客户端，用户可以在开发机命令终端（windows cmd/linux shell）下请求执行相应的hdc命令，运行于开发机器，其它的终端调试IDE也包含hdc client。

2. hdc server部分：作为后台进程也运行于开发机器，server管理client和设备端daemon之间通信包括连接的复用、数据通信包的收发，以及个别本地命令的直接处理。

3. hdc daemon部分：daemon部署于鸿蒙设备端作为守护进程来按需运行，负责处理来自client端的请求。

## 目录<a name="section161941989596"></a>

```
/developtools
├── hdc_standard      # hdc代码目录
│   └── src
│       ├── common    # 设备端和host端公用的代码目录
│       ├── daemon    # 设备端的代码目录 
│       ├── host      # host端的代码目录
│       ├── test      # 测试用例的代码目录 
│   └── prebuilt      # 预编译目录，存放预编译的二进制文件
```

### 使用说明<a name="section129654513264"></a>

hdc当前支持如下命令：

**表 1**  hdc命令列表

<a name="table159297571254"></a>
<table><thead align="left"><tr id="row149291357182511"><th class="cellrowborder" valign="top" width="50%" id="mcps1.2.3.1.1"><p id="p14423184344212"><a name="p14423184344212"></a><a name="p14423184344212"></a>Option</p>
</th>
<th class="cellrowborder" valign="top" width="50%" id="mcps1.2.3.1.2"><p id="p164237433425"><a name="p164237433425"></a><a name="p164237433425"></a>Description</p>
</th>
</tr>
</thead>
<tbody><tr id="row139291857142520"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1042344310428"><a name="p1042344310428"></a><a name="p1042344310428"></a>-t <em id="i198036018011"><a name="i198036018011"></a><a name="i198036018011"></a>key</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p19423174317428"><a name="p19423174317428"></a><a name="p19423174317428"></a>用于<span>指定连接该指定设备识Key</span></p>
<p id="p2014511479313"><a name="p2014511479313"></a><a name="p2014511479313"></a>举例：hdc -t  *****(设备id)  shell</p>
</td>
</tr>
<tr id="row1092965782514"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p04231743194215"><a name="p04231743194215"></a><a name="p04231743194215"></a>-s <em id="i510618125015"><a name="i510618125015"></a><a name="i510618125015"></a>socket</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p5424134314429"><a name="p5424134314429"></a><a name="p5424134314429"></a>用于<span>指定服务监听的socket配置</span></p>
<p id="p1599174953215"><a name="p1599174953215"></a><a name="p1599174953215"></a>举例：hdc -s ip:port</p>
</td>
</tr>
<tr id="row4929185718255"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p17424204354216"><a name="p17424204354216"></a><a name="p17424204354216"></a>-h/help -v/version</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p742444364214"><a name="p742444364214"></a><a name="p742444364214"></a><span>用于显示hdc相关的帮助、版本信息</span></p>
</td>
</tr>
<tr id="row169301574251"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p13424743134216"><a name="p13424743134216"></a><a name="p13424743134216"></a>list targets[-v]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p6424643164211"><a name="p6424643164211"></a><a name="p6424643164211"></a><span>显示所有已经连接的目标设备列表</span>，-v选项显示详细信息</p>
<p id="p423202318349"><a name="p423202318349"></a><a name="p423202318349"></a>举例： hdc list targets</p>
</td>
</tr>
<tr id="row139301957122519"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p8424164318423"><a name="p8424164318423"></a><a name="p8424164318423"></a>target mount</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p13424154324215"><a name="p13424154324215"></a><a name="p13424154324215"></a><span>以读写模式挂载/system等分区</span></p>
<p id="p23801376351"><a name="p23801376351"></a><a name="p23801376351"></a>举例： hdc target mount</p>
</td>
</tr>
<tr id="row5930657142518"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1642534318425"><a name="p1642534318425"></a><a name="p1642534318425"></a>smode [off]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p44253434422"><a name="p44253434422"></a><a name="p44253434422"></a>授予后台服务进程root权限， 使用off参数取消授权</p>
<p id="p9806102118436"><a name="p9806102118436"></a><a name="p9806102118436"></a>举例： hdc smode</p>
</td>
</tr>
<tr id="row893010573254"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1842544334210"><a name="p1842544334210"></a><a name="p1842544334210"></a>kill [-r]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p11425543124210"><a name="p11425543124210"></a><a name="p11425543124210"></a><span>终止hdc服务进程</span>, -r选项会触发再次重启hdc server</p>
<p id="p162862374437"><a name="p162862374437"></a><a name="p162862374437"></a>举例： hdc kill</p>
</td>
</tr>
<tr id="row865473444617"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p5655334184610"><a name="p5655334184610"></a><a name="p5655334184610"></a>start [-r]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p819155019464"><a name="p819155019464"></a><a name="p819155019464"></a><span>启动hdc服务进程</span>, -r选项会触发重启hdc server</p>
<p id="p219115074615"><a name="p219115074615"></a><a name="p219115074615"></a>举例： hdc start</p>
</td>
</tr>
<tr id="row1493015702512"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1542610433424"><a name="p1542610433424"></a><a name="p1542610433424"></a>tconn <em id="i82358142025"><a name="i82358142025"></a><a name="i82358142025"></a>host</em>[:<em id="i860817161021"><a name="i860817161021"></a><a name="i860817161021"></a>port</em>][-remove]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p17426174384210"><a name="p17426174384210"></a><a name="p17426174384210"></a>通过【ip地址：端口号】来指定连接的设备</p>
<p id="p15653482488"><a name="p15653482488"></a><a name="p15653482488"></a>举例： hdc tconn 192.168.0.100:10178</p>
</td>
</tr>
<tr id="row193125772516"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p542613431429"><a name="p542613431429"></a><a name="p542613431429"></a>tmode usb</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p19426174319422"><a name="p19426174319422"></a><a name="p19426174319422"></a><span>执行后设备端对应daemon进程重启，并首先选用usb连接方式</span></p>
</td>
</tr>
<tr id="row1184551114811"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1742614354215"><a name="p1742614354215"></a><a name="p1742614354215"></a>tmode port <em id="i1850518591411"><a name="i1850518591411"></a><a name="i1850518591411"></a>port-number</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p124267438425"><a name="p124267438425"></a><a name="p124267438425"></a><span>执行后设备端对应daemon进程重启，并优先使用网络方式连接设备，如果连接设备再选择usb连接</span></p>
</td>
</tr>
<tr id="row1322512145211"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p4427174394215"><a name="p4427174394215"></a><a name="p4427174394215"></a>fport list</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p19427184384213"><a name="p19427184384213"></a><a name="p19427184384213"></a><span>查看所有已建立的映射连接列表，显示的参数包括转发配置字符串，转发类型，转发参数</span></p>
</td>
</tr>
<tr id="row115461418165218"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p1842714364210"><a name="p1842714364210"></a><a name="p1842714364210"></a>fport rm <em id="i245711341319"><a name="i245711341319"></a><a name="i245711341319"></a>local</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p342724319424"><a name="p342724319424"></a><a name="p342724319424"></a><span>删除已映射的给定配置字符串连接(配置字符串值可以通过list获取)，没有指定则删除所有</span></p>
</td>
</tr>
<tr id="row1157737165213"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p442704315428"><a name="p442704315428"></a><a name="p442704315428"></a>file send<em id="i34274438428"><a name="i34274438428"></a><a name="i34274438428"></a> </em><em id="i6958481309"><a name="i6958481309"></a><a name="i6958481309"></a>local remote</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p12427114316425"><a name="p12427114316425"></a><a name="p12427114316425"></a><span>从host端发送文件至设备</span>端</p>
<p id="p292614408162"><a name="p292614408162"></a><a name="p292614408162"></a>举例： hdc file send  E:\a.txt  /data/local/tmp</p>
</td>
</tr>
<tr id="row8748171465317"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p7427164310425"><a name="p7427164310425"></a><a name="p7427164310425"></a>file recv [-a] <em id="i1880435111020"><a name="i1880435111020"></a><a name="i1880435111020"></a>remote local</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p19427143174220"><a name="p19427143174220"></a><a name="p19427143174220"></a><span>从设备端拉出文件至本地</span>host端</p>
<p id="p191761424101713"><a name="p191761424101713"></a><a name="p191761424101713"></a>举例： hdc file recv  /data/local/tmp/a.txt   ./</p>
</td>
</tr>
<tr id="row887171025420"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p204287432425"><a name="p204287432425"></a><a name="p204287432425"></a>install<em id="i242704315422"><a name="i242704315422"></a><a name="i242704315422"></a> </em>[-r/-d/-g]<em id="i642814310424"><a name="i642814310424"></a><a name="i642814310424"></a> </em><em id="i103610557016"><a name="i103610557016"></a><a name="i103610557016"></a>package</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p12428194312421"><a name="p12428194312421"></a><a name="p12428194312421"></a><span>安装</span><span id="text242884314423"><a name="text242884314423"></a><a name="text242884314423"></a>OpenHarmony</span><span> package</span></p>
<p id="p1419642611411"><a name="p1419642611411"></a><a name="p1419642611411"></a>举例： hdc install ***.hap</p>
</td>
</tr>
<tr id="row1973583819549"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p20428943104214"><a name="p20428943104214"></a><a name="p20428943104214"></a>uninstall [-k] <em id="i84129581508"><a name="i84129581508"></a><a name="i84129581508"></a>package</em></p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p0428144314429"><a name="p0428144314429"></a><a name="p0428144314429"></a><span>卸载</span><span id="text442834344220"><a name="text442834344220"></a><a name="text442834344220"></a>OpenHarmony</span><span>应用</span></p>
</td>
</tr>
<tr id="row1513010417560"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p84281143184214"><a name="p84281143184214"></a><a name="p84281143184214"></a>hilog</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p11428343144216"><a name="p11428343144216"></a><a name="p11428343144216"></a><span>支持查看抓取hilog调试信息</span></p>
<p id="p555163671514"><a name="p555163671514"></a><a name="p555163671514"></a>举例： hdc hilog</p>
</td>
</tr>
<tr id="row119311957172516"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.1 "><p id="p144297439423"><a name="p144297439423"></a><a name="p144297439423"></a>shell<em id="i15429144314210"><a name="i15429144314210"></a><a name="i15429144314210"></a> </em>[<em id="i04791261510"><a name="i04791261510"></a><a name="i04791261510"></a>command</em>]</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.2.3.1.2 "><p id="p16429843124213"><a name="p16429843124213"></a><a name="p16429843124213"></a><span>远程执行命令或进入交互命令环境</span></p>
<p id="p1490692061519"><a name="p1490692061519"></a><a name="p1490692061519"></a>举例： hdc shell</p>
</td>
</tr>
</tbody>
</table>

以下是常用hdc命令示例，供开发者参考：

-   查看设备连接信息

    ```
    hdc list targets
    ```


-   往设备中推送文件

    ```
    hdc file send  E:\a.txt  /data/local/tmp
    ```

-   重启设备

    ```
    hdc target boot
    ```


-   查看日志

    ```
    hdc hilog
    ```

-   进入命令行交互模式

    ```
    hdc shell
    ```

-   配置服务监听的socket。

    ```
    hdc -s 192.168.1.100:1234 
    ```


-   重启至bootloader模式。

    ```
    hdc target boot bootloader
    ```


-   网络连接。

    ```
    hdc tconn 192.168.0.100:8710
    ```


## 相关仓<a name="section1371113476307"></a>

研发工具链子系统

**developtools\_hdc\_standard**

