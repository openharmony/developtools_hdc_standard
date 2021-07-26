# 几点说明:

[1.通过git clone方式下载](#section662115419449)
建议通过gitclone方式下载该仓
外仓命令为:
**git clone git@gitee.com:openharmony/developtools_hdc_standard.git**
合作仓命令为:
**git clone git@gitee.com:OHOS_STD/developtools_hdc_standard.git**

[2.通过网页形式下载](#section15908143623714)
通过网页形式下载prebuilt，请 使用类似如下URL打开网页  https://gitee.com/openharmony/developtools_hdc_standard/blob/master/prebuilt/windows/hdc-std.exe ，点击中间下载方式进行下载，windows版本文件大小在**5M左右**，linux版本在**2M左右**，不要使用右击另存为方式进行保存下载，下载后检查文件大小(说三遍)。

[3.支持环境](#section161941989596)
支持运行环境 linux版本建议ubuntu20 CentOS8 64位，其他版本相近也应该可以，libc++.so引用错误请使用ldd/readelf等命令检查库引用 windows版本建议windows10 64位，windows8也应该可以，Windows7等EOF版本尚未测试，如果低版本windows winusb库缺失，请使用zadig更新库。

[4.关于Issue](#section161941989596)
近期hdc刚开发完成，适配和调整变动较多，如果遇到异常情况，建议按照如下步骤进行排查:
1)首先核对server与daemon版本是否匹配，hdc-std -v, hdcd -v。
2)更新工程最新的线上代码和预编译文件，是否在后续版本中已解决问题。
3)规范的和详细的提出issue，我们将尽快跟进。
