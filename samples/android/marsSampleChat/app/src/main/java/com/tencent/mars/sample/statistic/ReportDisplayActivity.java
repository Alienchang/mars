package com.tencent.mars.sample.statistic;

import android.support.v4.app.FragmentManager;
import android.support.v4.app.FragmentTransaction;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.support.v7.widget.Toolbar;
import android.widget.RadioButton;
import android.widget.RadioGroup;

import com.tencent.mars.sample.R;
import com.tencent.mars.xlog.Log;

import utils.bindsimple.BindSimple;
import utils.bindsimple.BindView;

public class ReportDisplayActivity extends AppCompatActivity implements RadioGroup.OnCheckedChangeListener {

    public static String TAG = ReportDisplayActivity.class.getSimpleName();

    @BindView(R.id.main_sheet)
    RadioGroup mMainSheet;

    @BindView(R.id.display_toolbar)
    Toolbar mToolbar;

    FragmentManager mFragmentManager;

    FlowReportFragment flowReportFragment;
    SdtReportFragment sdtReportFragment;
    TaskReportFragment taskReportFragment;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_report_display);
        BindSimple.bind(this);

        setSupportActionBar(mToolbar);
        getSupportActionBar().setDisplayHomeAsUpEnabled(false);  //是否显示显示返回箭头
        getSupportActionBar().setDisplayShowTitleEnabled(false); //是否显示标题

        mFragmentManager = getSupportFragmentManager();

        ((RadioButton)mMainSheet.getChildAt(0)).setChecked(true);
        onCheckedChanged(mMainSheet, 1);

        mMainSheet.setOnCheckedChangeListener(this);
    }

    @Override
    public void onCheckedChanged(RadioGroup group, int checkedId) {
        FragmentTransaction fragmentTrans = mFragmentManager.beginTransaction();
        hideFragments(fragmentTrans);
        int id = (checkedId%3 == 0 ? 3 : (checkedId%3));
        switch (id) {
            case 1:
                if (taskReportFragment == null) {
                    taskReportFragment = new TaskReportFragment();
                    fragmentTrans.add(R.id.dis_ll_fragment, taskReportFragment);
                }
                else {
                    fragmentTrans.show(taskReportFragment);
                }
                break;
            case 2:
                if (flowReportFragment == null) {
                    flowReportFragment = new FlowReportFragment();
                    fragmentTrans.add(R.id.dis_ll_fragment, flowReportFragment);
                }
                else {
                    fragmentTrans.show(flowReportFragment);
                }
                break;
            case 3:
                if (sdtReportFragment == null) {
                    sdtReportFragment = new SdtReportFragment();
                    fragmentTrans.add(R.id.dis_ll_fragment, sdtReportFragment);
                }
                else {
                    fragmentTrans.show(sdtReportFragment);
                }
                break;
            default:
                break;
        }
        fragmentTrans.commit();

    }

    private void hideFragments(FragmentTransaction transaction) {
        if (flowReportFragment != null) {
            transaction.hide(flowReportFragment);
        }
        if (sdtReportFragment != null) {
            transaction.hide(sdtReportFragment);
        }
        if (taskReportFragment != null) {
            transaction.hide(taskReportFragment);
        }
    }
}
