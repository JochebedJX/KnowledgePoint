```c++
#define _CRT_SECURE_NO_WARNINGS 1
#include<iostream>
#include<vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 10
using namespace std;

template<class T>
class Sort{
public:
	Sort(vector<T>& arr) :vec(arr){}

	void InsertSort()
	{
		int size = vec.size();
		int key = vec[0];
		int i = 0, j = 0;
		for (i = 1; i < size; i++)
		{
			key = vec[i];
			j = i - 1;
			while (j >= 0)
			{
				if (key < vec[j])
				{
					vec[j + 1] = vec[j];
					j--;
				}
				else
					break;
			}
			vec[j + 1] = key;
		}
	}

	void SelectSort()
	{
		int size = vec.size();
		int i = 0, j = 0;
		for (i = 0; i < size; i++)
		{
			int min = vec[i];
			int k = i;
			for (j = i + 1; j < size; j++)
			{
				if (vec[j] < min)
				{
					min = vec[j];
					k = j;
				}
			}
			if (k != i)
			{
				swap(vec[k], vec[i]);
			}
		}
	}

	void maxheap_down(vector<T>& a, int start, int end)
	{
		int father = start;	// 当前结点的位置
		int left = 2 * father + 1;	// 左孩子的位置
		int tmp = a[father];	// 当前节点的大小
		for (; left <= end; father = left, left = 2 * left + 1)
		{
			if (left<end&&a[left]<a[left + 1])
				++left;	// 左右孩子中选择较大者
			if (tmp >= a[left])
				break;	//调整结束
			else
			{
				// 交换值
				a[father] = a[left];
				a[left] = tmp;
			}
		}
	}
	void HeapSort()
	{
		int n = vec.size();
		int i;
		for (i = n / 2 - 1; i >= 0; --i)
		{
			maxheap_down(vec, i, n - 1);
		}
		for (i = n - 1; i >= 0; --i)
		{
			swap(vec[0], vec[i]);
			maxheap_down(vec, 0, i - 1);
		}
	}

	int quicksort(vector<T>& vec, int left, int right)
	{
		int i = left;
		int j = right;
		while (i < j)
		{
			while(i<j&&vec[i] <= vec[j])
			{
				i++;
			}
			if (i<j)
			{
				swap(vec[i], vec[j]);
				j--;
			}
			while (i<j&&vec[i] <= vec[j])
			{
				j--;
			}
			if (i<j)
			{
				swap(vec[i], vec[j]);
				i++;
			}
		}
		return i;
	}

	void QuickSort(int left,int right)
	{
		if (left < right)
		{
			int pos = quicksort(vec, left, right);
			QuickSort(left, pos - 1);
			QuickSort(pos + 1, right);
		}
	}

	void BubbleSort()
	{
		int size = vec.size();
		int i = 0, j = 0;
		for (i = 0; i < size; i++)
		{
			for (j = 0; j < size - i-1; j++)
			{
				if (vec[j]>vec[j + 1])
				{
					swap(vec[j], vec[j + 1]);
				}
			}
		}
	}

	void Merge(vector<T>& a, int mid, int left, int right, int *tmp)
	{
		int len = a.size();
		
		int index = left;
		int i = left, j = mid+1;
		
		while (i <= mid&&j <= right){
			if (a[i] <= a[j])
				tmp[index++] = a[i++];
			else
				tmp[index++] = a[j++];
		}
		while (i <= mid)
			tmp[index++] = a[i++];
		while (j <= right)
			tmp[index++] = a[j++];

		for (int k = left; k < right + 1; k++)
		{
			a[k] = tmp[k];
		}
		
	}

	void MergeSort(int left, int right,int* tmp)
	{
		if (left < right)
		{
			int mid = (left + right) / 2;
			MergeSort(left, mid, tmp);
			MergeSort(mid + 1, right,tmp);
			Merge(vec, mid, left, right,tmp);
		}
	}

	void Print()
	{
		for (int i = 0; i < vec.size(); i++)
			cout << vec[i] << " ";
		cout<< endl;
	}
private:
	vector<T> vec;
};

int main()
{
	vector<int> vec;
	vec.push_back(3);
	vec.push_back(4);
	vec.push_back(1);
	vec.push_back(9);
	vec.push_back(6);
	vec.push_back(2);
	vec.push_back(7);
	vec.push_back(8);
	vec.push_back(10);
	vec.push_back(5);
	Sort<int> s(vec);
	//s.InsertSort();
	//s.SelectSort();
	//s.BubbleSort();
	//s.HeapSort();
	//s.QuickSort(0,vec.size()-1);
	int len = vec.size();
	int *tmp = (int *)malloc(sizeof(int)*len);
	for (int i = 0; i < len; i++)
	{
		tmp[i] = -1;
	}
	s.MergeSort(0, len - 1,tmp);
	s.Print();
	free(tmp);
	system("pause");
	return 0;
}

```

